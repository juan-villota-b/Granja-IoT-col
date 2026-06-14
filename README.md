# Granja IoT — Sistema de Agricultura de Precision

Red de sensores **OpenThread** con 4 nodos **ESP32-C6**, gateway local **ThingsBoard Edge**, dashboard de monitoreo con **Leaflet + Chart.js**, control automatico de riego, y acceso remoto via **Tailscale Funnel**.

## Arquitectura general

```
                        ┌──────────────────────────────────────┐
                        │ PC Desarrollo (Ubuntu 24.04)          │
                        │  ┌──────────────────────────────────┐ │
                        │  │ TB Edge 4.2.0EDGE :8080          │ │
                        │  │ Database interna (HSQLDB)          │ │
                        │  │                                    │ │
                        │  │ Granja Dashboard Docker :3000     │ │
                        │  │ (FastAPI + Leaflet + Chart.js)    │ │
                        │  │ → REST API → TB Edge :8080        │ │
                        │  │ → Mobile-first responsive          │ │
                        │  │ → Tailscale Funnel (publico)       │ │
                        │  └──────────────────────────────────┘ │
                        └──────────────────────────────────────┘

                ┌──────────────────▼──────────────────────────┐
                │      Raspberry Pi 4 (finca@192.168.1.114)    │
                │  ┌────────────────────────────────────────┐  │
                │  │ TB Edge 4.2.0EDGE :8082                │  │
                │  │ PostgreSQL 16                           │  │
                │  │ OTBR (nRF52840 RCP)                     │  │
                │  │ Bridge CoAP :5685 + MQTT Gateway :1883  │  │
                │  │ IrrigationController (python)           │  │
                │  └────────────────────────────────────────┘  │
                └──────────┬──────────────────────────────────┘
                           │ Thread 802.15.4
        ┌──────────────────┼───────────────────┐
        │                  │                   │
   ┌────▼────┐  ┌─────────▼──┐  ┌──────────▼──┐  ┌─────▼─────┐
   │Nodo-Luz │  │Nodo-Temperatura│ │ Nodo-Humedad│  │  BOMBA   │
   │SED·LDR  │  │  SED · DHT22  │  │SED · HW-390 │  │SED·relay │
   │ light%  │  │    temp°C     │  │soil humid%  │  │ valve 0/1│
   └─────────┘  └──────────────┘  └─────────────┘  └──────────┘
```

## Estructura del repositorio

| Carpeta | Contenido |
|---------|-----------|
| [`Nodes-v2/`](Nodes-v2/) | Firmware ESP32-C6 SED — 4 nodos (Luz, Temperatura, Humedad, BOMBA) |
| [`Raspberry-v2/`](Raspberry-v2/) | Stack Docker RPi: OTBR + TB Edge + PostgreSQL + Bridge + riego automatico |
| [`otbr/`](otbr/) | Entorno de desarrollo PC: Granja Dashboard (FastAPI + Leaflet + Chart.js, puerto 3000) |
| [`thingsboard-docker/`](thingsboard-docker/) | ThingsBoard CE — servidor central con PostgreSQL |

## Flujo de datos

```
Sensor ESP32-C6
  │  Lee sensor cada 30s (1s para BOMBA)
  │  Aplica umbrales → decide si enviar
  ├──► POST CBOR /readings → Bridge CoAP :5685 (RPi)
  │     └── Bridge decodifica CBOR → publica MQTT gateway
  │           └── TB Edge (RPi) recibe → almacena en PostgreSQL
  │                 └── Cloud RPC sync → TB CE (PC) :7070
  │                       └── Dashboard consulta TB CE :8080 (REST API)
  │                             └── Leaflet mapa + Chart.js graficas
  │
  │     IrrigationController (en el bridge RPi):
  │       └── Evalua humedad suelo, luz, temp, hora
  │           └── ABRIR (hum < 30%) / CERRAR (hum >= 70%)
  │               └── RPC directo a TB Edge :8082
  │                   └── Bridge encola comando piggyback al actuador

Comando valvula manual:
  Dashboard → TB CE :8080 → Cloud RPC → TB Edge → MQTT → Bridge → piggyback CoAP → BOMBA ejecuta
```

## Hardware

| Componente | Rol |
|-----------|-----|
| **ESP32-C6** (x4) | Nodos sensor/actuador con radio Thread 802.15.4 nativa |
| **nRF52840** | RCP (Radio Co-Processor) para el OTBR |
| **Raspberry Pi 4** | Gateway de campo: OTBR + TB Edge + Bridge + PostgreSQL |
| **PC/Servidor** | ThingsBoard CE + Dashboard web (FastAPI) |

## Puertos de red

| Puerto | Servicio | Donde corre |
|--------|----------|-------------|
| `:3000` | Granja Dashboard (FastAPI + Leaflet) | PC (Docker) |
| `:8080` | ThingsBoard CE (web + API) | PC (host) |
| `:8082` | ThingsBoard Edge (web) | RPi |
| `:1883` | ThingsBoard Edge (MQTT) | RPi |
| `:8083` | OTBR Web GUI | RPi |
| `:8081` | OTBR REST API | RPi |
| `:5685` | Bridge CoAP Server (UDP) | RPi |

## Dashboard — Paginas

| Pagina | Funcion |
|--------|---------|
| **Dashboard** | Mapa Leaflet con sensores geolocalizados + panel de telemetria en tiempo real (WebSocket) + graficas scrolling con ventana de 1h |
| **Historicos** | Consulta por rango de fechas + selector de intervalo de agregacion + grafico + tabla paginada + exportacion CSV |
| **Monitoreo** | Grafica unificada: 3 sensores (temp/hum/luz) sobrepuestos + periodos de riego como overlay azul + crosshair con tooltip independiente por dataset |
| **Valvula** | Control ABRIR/CERRAR con botones tactiles + estado en tiempo real |
| **Agregar Nodo** | Formulario con minimapa para geolocalizar nuevos nodos + auto-asignacion a customer/edge |

## Nodos

| Nodo | Sensor | NODE_ID | Medicion |
|------|--------|---------|----------|
| Nodo-Luz | LDR | Nodo-Luz | light% (porcentaje de luz) |
| Nodo-Temperatura | DHT22 | Nodo-Temperatura | temperature°C |
| Nodo-Humedad | HW-390 | Nodo-Humedad | soil humidity% |
| BOMBA | Relay GPIO7 | BOMBA | valve 0/1 (actuador) |

## Control automatico de riego

El bridge (en la RPi) ejecuta `Raspberry-v2/bridge/automation/irrigation.py` que evalua toda la telemetria entrante:

- **ABRIR** valvula si humedad suelo < 30% (y luz >= 10%, temp < 35°C, hora 6-23h, cooldown >= 60s)
- **CERRAR** valvula inmediatamente si humedad >= 70%
- Los comandos RPC van directo a TB Edge en la RPi (`192.168.1.114:8082`)

Umbrales en `Raspberry-v2/bridge/config.yaml`:

```yaml
irrigation:
  enabled: true
  soil_low_pct: 30
  soil_high_pct: 70
  light_min_pct: 10
  temp_max_c: 35.0
  min_cycle_seconds: 60
  allowed_start_hour: 6
  allowed_end_hour: 23
```

## Acceso remoto (Tailscale Funnel)

El dashboard esta expuesto al internet via **Tailscale Funnel** con HTTPS:

```
https://granja-iot.tailaf11de.ts.net
```

Para activarlo en otro equipo:

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
sudo tailscale serve --bg http://localhost:3000
sudo tailscale funnel --bg 3000
```

## Usuarios multi-tenant

El dashboard soporta usuarios tipo **CUSTOMER_USER** de ThingsBoard:

- El backend filtra devices y edges segun el `customer_id` del usuario logueado
- Los customer users solo ven su edge asignado y sus dispositivos
- Al crear un nodo como customer user, se asigna automaticamente al customer

Para crear un customer con su usuario:

```bash
cd otbr/granja-dashboard && python3 setup_customer.py
```

## Tecnologias

| Capa | Tecnologia |
|------|-----------|
| Red IoT | Thread (IEEE 802.15.4) + 6LoWPAN + IPv6 |
| Aplicacion IoT | CoAP + CBOR (binario compacto) |
| Gateway | OpenThread Border Router (Docker) |
| Plataforma IoT | ThingsBoard Edge 4.2.0EDGE |
| Bridge | Python (aiocoap + paho-mqtt) |
| Dashboard | FastAPI + Leaflet + Chart.js (Vanilla JS SPA) |
| Almacenamiento | PostgreSQL 16 (RPi) / HSQLDB (PC local) |
| Contenedores | Docker Compose |
| Acceso remoto | Tailscale Funnel (HTTPS publico) |

## Credenciales por defecto

| Sistema | URL | Usuario | Contrasena |
|---------|-----|---------|------------|
| ThingsBoard CE | `http://localhost:8080` | `tenant@thingsboard.org` | `tenant` |
| ThingsBoard Edge (RPi) | `http://192.168.1.114:8082` | `tenant@thingsboard.org` | `tenant` |
| Dashboard | `http://localhost:3000` | `tenant@thingsboard.org` | `tenant` |
| Dashboard (Funnel) | `https://granja-iot.tailaf11de.ts.net` | `tenant@thingsboard.org` | `tenant` |
| Customer user | `http://localhost:3000/login` | `juan@finca.com` | `juan123` |
| RPi "finca" | `ssh finca@192.168.1.114` | `finca` | `12345` |

## Como empezar

### 1. Levantar ThingsBoard Edge (PC local)

TB CE debe estar corriendo en `localhost:8080`. Si usas Docker:

### 2. Levantar el Dashboard

```bash
cd otbr
./start.sh granja-dashboard   # build + run
# Abrir http://localhost:3000
```

### 3. Levantar el stack de campo (RPi)

```bash
ssh finca@192.168.1.114
cd ~/Raspberry-v2
./start.sh
```

### 4. Flashear nodos ESP32-C6

```bash
cd Nodes-v2/Nodo-sensor-SEDv2-ldr
. ~/.espressif/v5.5.4/esp-idf/export.sh
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.sed" idf.py reconfigure
idf.py build
idf.py -p /dev/ttyACM1 flash

# Repetir para SEDv2-temp (Temperatura), SEDv2-hum (Humedad), SEDv2-valve (BOMBA)
# Cambiar el puerto segun corresponda
```
