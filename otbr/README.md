# OTBR — Granja Dashboard + Bridge + Stack IoT

Infraestructura IoT de desarrollo local: **Dashboard web (FastAPI + Leaflet + Chart.js), Bridge Python (CoAP server + MQTT gateway + irrigation controller) y herramientas OTBR**.

La arquitectura de produccion corre en **Raspberry Pi 4** (ver [`Raspberry-v2/`](../Raspberry-v2/)).

Los 4 nodos ESP32-C6 SED (Luz, Temperatura, Humedad, BOMBA) envian telemetria via CoAP/CBOR al Bridge, que la publica via MQTT a ThingsBoard Edge.

## Arquitectura

```
                       WiFi / Ethernet
 ┌──────────────────────────────────────────────────────────────────────┐
 │  HOST (Ubuntu 24.04)                                                  │
 │                                                                       │
 │  ┌──────────────────────────────────────────────────────────────┐   │
 │  │  Docker Compose                                                │   │
 │  │                                                                │   │
 │  │  ┌─────────────────────┐                                       │   │
 │  │  │  Granja Dashboard   │──── REST API ──► TB Edge :8080 (host) │   │
 │  │  │  :3000              │                                       │   │
 │  │  │  Mobile-First SPA   │                                       │   │
 │  │  │  + Tailscale Funnel │                                       │   │
 │  │  └─────────────────────┘                                       │   │
 │  └──────────────────────────────────────────────────────────────┘   │
 └─────────────────────────────────────────────────────────────────────┘

        ┌──────────────────────────────────────────────┐
        │  RPi 4 (finca@192.168.1.114) - Produccion    │
        │  ┌─────────────────────────────────────────┐  │
        │  │  OTBR (nRF52840 RCP)                     │  │
        │  │  TB Edge :8082 + PostgreSQL              │  │
        │  │  Bridge CoAP :5685 + MQTT gateway        │  │
        │  │  + IrrigationController                  │  │
        │  └─────────────────────────────────────────┘  │
        └──────────────────┬───────────────────────────┘
                           │ Thread 802.15.4
              ┌────────────┼────────────┐
              │            │            │
        ┌─────▼─────┐ ┌───▼────┐ ┌─────▼─────┐ ┌─────▼─────┐
        │ Nodo-Luz  │ │ Nodo-T │ │ Nodo-H   │ │  BOMBA   │
        │ SED · LDR │ │ SED · DHT │ │ SED · HW │ │ SED · relay│
        └───────────┘ └────────┘ └──────────┘ └───────────┘

Flujo de datos:
ESP32-C6 ──POST CBOR──→ Bridge:5685 ──MQTT──→ TB Edge:1883 ──REST API──→ Dashboard:3000
  {id, t, h, b, r, u}    decode+publish       telemetry topic        chart + mapa

Flujo de control:
Dashboard:3000 ──REST──→ TB Edge ──MQTT──→ Bridge ──CoAP downlink──→ BOMBA
  valve 0/1              RPC         gateway      piggyback
```

## Servicios

| Servicio | Puerto | Descripcion |
|----------|--------|-------------|
| Granja Dashboard | 3000 | FastAPI + Leaflet + Chart.js — mobile-first SPA |
| TB Edge (host) | 8080 | ThingsBoard Edge 4.2.0EDGE — API REST + MQTT |

## URLs de acceso

| URL | Que es |
|-----|--------|
| `https://granja-iot.tailaf11de.ts.net` | **Dashboard publico** (Tailscale Funnel HTTPS) |
| `http://localhost:3000` | Granja Dashboard local |
| `http://localhost:8080` | ThingsBoard Edge Web UI |
| `http://192.168.1.114:8082` | TB Edge en la RPi |
| `http://192.168.1.114:8083` | OTBR Web GUI |

## Puesta en marcha

### 1. Asegurar que TB Edge corre en el host

TB Edge 4.2.0EDGE debe estar instalado y corriendo en `localhost:8080`.

### 2. Levantar el Dashboard

```bash
cd ~/Documentos/Semestre_VII/Granja-IOT/otbr
./start.sh granja-dashboard  # build + run
```

> **NUNCA usar `docker compose up` sin `--build`** — la imagen cacheada puede estar desactualizada.
> `./start.sh` siempre fuerza `--build`.

### 3. Verificar

```bash
# Logs del dashboard
docker logs -f granja-dashboard

# Abrir http://localhost:3000 → login con credenciales TB
```

## Dashboard — Paginas y caracteristicas

### Paginas
| Pagina | Funcion |
|--------|---------|
| Dashboard | Mapa Leaflet + panel de telemetria en tiempo real (WebSocket) + graficas con ventana deslizante de 1h |
| Historicos | Consulta por rango personalizado + intervalo de agregacion + grafico + tabla paginada + CSV |
| Monitoreo | 3 sensores sobrepuestos (temp/hum/luz) + overlay de periodos de riego + crosshair |
| Valvula | Control ABRIR/CERRAR con botones tactiles + estado en tiempo real |
| Agregar Nodo | Formulario con minimapa Leaflet + auto-asignacion a edge/customer |

### Caracteristicas tecnicas
- **Mobile-first**: bottom tab bar, drawer lateral, layouts adaptados (< 768px)
- **Tema claro/oscuro**: persistido en localStorage, toggle en sidebar + top bar mobile
- **Graficas en tiempo real**: WebSocket con timestamp de TB, eje X lineal con scrolling
- **Auto-deteccion de sensor**: cada nodo detecta su variable (temp/hum/light/valve) via atributos
- **Customer users**: filtrado por customer_id, edge bloqueado para CUSTOMER_USER
- **Tailscale Funnel**: acceso HTTPS publico sin abrir puertos del router

### Backend (FastAPI)
- `POST /api/login`, `POST /api/logout` — autenticacion contra TB
- `GET /api/devices` — dispositivos del tenant/customer con telemetria y atributos
- `GET /api/telemetry/{id}/history?keys=&startTs=&endTs=&agg=&interval=&limit=` — datos historicos
- `GET /api/monitoreo/history?deviceIds=&startTs=&endTs=` — datos para pagina Monitoreo
- `POST /api/rpc/valve` — enviar comando RPC set_valve
- `GET /api/edges`, `GET /api/me` — metadatos

## Acceso publico por internet (Tailscale Funnel)

```bash
# Instalar
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up

# Exponer
sudo tailscale serve --bg http://localhost:3000
sudo tailscale funnel --bg 3000

# URL generada: https://granja-iot.tailaf11de.ts.net
```

## Bridge — Como funciona

El bridge expone un **servidor CoAP en `[::]:5685`** con un recurso:

| URI | Metodo | Payload | Respuesta |
|-----|--------|---------|-----------|
| `/readings` | POST | CBOR `{id, t, h, b, r, u, ...}` | CBOR `{v: 0/1}` (comando valvula) |

Cada nodo reporta keys especificas segun su tipo de sensor:
- **Nodo-Luz**: `light` (uint8, %)
- **Nodo-Temperatura**: `temperature` (half-float, °C)
- **Nodo-Humedad**: `humidity` (uint8, %)
- **BOMBA**: `valve` (uint8, 0/1)

### Control automatico de riego

El `IrrigationController` (`bridge/automation/irrigation.py`) evalua toda la telemetria entrante y decide cuando abrir/cerrar la valvula. Logica detallada en `AGENTS.md`.

## Estructura del proyecto

```
otbr/
├── docker-compose.yml        # Servicio Granja Dashboard
├── start.sh                  # Entrypoint: siempre --build
├── AGENTS.md                 # Instrucciones de desarrollo
├── granja-dashboard/         # FastAPI dashboard (puerto 3000)
│   ├── Dockerfile
│   ├── setup_customer.py     # Script para crear customer + user en TB
│   ├── app/
│   │   ├── static/js/        # map.js, realtime.js, historicos.js, valve.js, monitoreo.js, app.js
│   │   ├── static/css/       # style.css
│   │   ├── templates/        # base.html, login.html, dashboard.html
│   │   ├── main.py           # API FastAPI
│   │   ├── tb_client.py      # Cliente HTTP ThingsBoard
│   │   └── config.py
│   └── requirements.txt
├── bridge/                   # Bridge Python (CoAP Server + MQTT Gateway + Irrigation)
│   ├── main.py
│   ├── config.yaml
│   ├── mqtt/
│   ├── automation/
│   │   └── irrigation.py
│   ├── start.sh
│   └── Dockerfile
└── README.md
```

```
Granja-IOT/
├── Nodes-v2/                 # Firmware ESP32-C6 SED (4 nodos)
│   ├── Nodo-sensor-SEDv2/    # Nodo-Luz (LDR, light%)
│   ├── Nodo-sensor-SEDv2-2/  # Nodo-Temperatura (DHT22, temp°C)
│   ├── Nodo-sensor-SEDv2-3/  # Nodo-Humedad (HW-390, soil humidity%)
│   └── Nodo-sensor-SEDv2-4/  # BOMBA (relay GPIO7, valve 0/1)
├── Raspberry-v2/             # Stack Docker para RPi gateway (produccion)
├── otbr/                     # Stack de desarrollo local + dashboard
└── Nodes-legacy/             # Firmware antiguo (obsoleto)
```

## Credenciales

| Sistema | Usuario | Contrasena |
|---------|---------|------------|
| ThingsBoard Edge | `tenant@thingsboard.org` | `tenant` |
| Dashboard (admin) | `tenant@thingsboard.org` | `tenant` |
| Dashboard (customer) | `juan@finca.com` | `juan123` |
| RPi SSH | `finca@192.168.1.114` | `12345` |

## Solucion de problemas

### El dashboard no carga datos

```bash
# Verificar que TB Edge esta corriendo
curl http://localhost:8080/api/system/info

# Ver logs del dashboard
docker logs -f granja-dashboard
```

### Puerto 3000 en uso

```bash
sudo lsof -i :3000
docker compose down && docker compose up -d --build granja-dashboard
```
```
