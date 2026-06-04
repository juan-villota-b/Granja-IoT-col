# Granja IoT — Sistema de Agricultura de Precisión

Red de sensores **OpenThread** con nodos **ESP32-C6**, gateway local **ThingsBoard Edge**, servidor central **ThingsBoard CE** y dashboard de monitoreo en tiempo real con **Leaflet + Chart.js**.

## Arquitectura general

```
                            ┌──────────────────────┐
                            │   THINGSBOARD CE     │
                            │   Servidor central    │
                            │   :8080 web, :7070 RPC│
                            └──────────┬───────────┘
                                       │ Cloud RPC
                    ┌──────────────────┼──────────────────┐
                    │                  │                  │
           ┌────────▼────────┐  ┌─────▼───────┐  ┌──────▼──────┐
           │  EDGE 1 (RPi)   │  │  EDGE 2     │  │  EDGE N     │
           │  otbr + bridge  │  │  (embebido) │  │  (...)      │
           │  TB Edge :8082  │  │             │  │             │
           └────────┬────────┘  └─────────────┘  └─────────────┘
                    │ Thread 802.15.4
           ┌────────┴────────┐
           │ ESP32-C6 NODOS  │
           │ SED / Router    │
           │ CoAP + CBOR     │
           └─────────────────┘
```

## Estructura del repositorio

| Carpeta | Contenido |
|---------|-----------|
| [`nodo-coap-sed/`](nodo-coap-sed/README.md) | Firmware ESP32-C6 SED — nodo sensor TH ultra bajo consumo (~15 µA) |
| [`nodo-coap-sed-2/`](nodo-coap-sed-2/README.md) | Segundo nodo SED (copia con configuración propia) |
| [`otbr/`](otbr/README.md) | Stack Docker: OTBR + TB Edge + PostgreSQL + Bridge Python |
| [`sistema-embebido/`](sistema-embebido/README.md) | Stack mínimo (4 servicios) para desplegar en dispositivo embebido |
| [`thingsboard-docker/`](thingsboard-docker/README.md) | ThingsBoard CE + PostgreSQL (servidor central) |
| [`Raspberry-pi-4/`](Raspberry-pi-4/README.md) | Especificaciones y config de la RPi gateway "finca" |

## Flujo de datos

```
Sensor ESP32-C6
  │  Lee temp/hum/batería/RSSI cada 30s
  │  Aplica umbrales (0.5°C / 3%) → decide si enviar
  ├──► POST CBOR /readings → Bridge CoAP :5685
  │     └── Bridge decodifica CBOR → publica MQTT
  │           └── TB Edge recibe → almacena en PostgreSQL
  │                 └── Edge sincroniza con CE vía Cloud RPC
  │                       └── Dashboard consulta CE :8080
  │                             └── Leaflet mapa + Chart.js

Comando válvula:
  Dashboard → CE :8080 → Cloud RPC :7070 → Edge :8082
    → MQTT v1/gateway/rpc → Bridge → encola comando
      → Siguiente POST del nodo recibe piggyback → ejecuta
```

## Hardware

| Componente | Rol |
|-----------|-----|
| **ESP32-C6** | Nodo sensor con radio Thread 802.15.4 nativa |
| **nRF52840** | RCP (Radio Co-Processor) para el OTBR |
| **Raspberry Pi 4** | Gateway de campo: OTBR + TB Edge + Bridge |
| **PC/Servidor** | ThingsBoard CE + Dashboard web |
| **Sensores** | DHT22/BME280 (temp/humedad), relé + válvula solenoide |

## Puertos de red

| Puerto | Servicio | Dónde corre |
|--------|----------|-------------|
| `:3000` | Granja Dashboard (FastAPI + Leaflet) | PC/Servidor |
| `:8080` | ThingsBoard CE (web + API) | PC/Servidor |
| `:7070` | ThingsBoard CE (Cloud RPC) | PC/Servidor |
| `:8082` | ThingsBoard Edge (web) | RPi / Embebido |
| `:1884` | ThingsBoard Edge (MQTT) | RPi / Embebido |
| `:5684` | ThingsBoard Edge (CoAP) | RPi / Embebido |
| `:8083` | OTBR Web GUI | RPi / Embebido |
| `:8081` | OTBR REST API | RPi / Embebido |
| `:5685` | Bridge CoAP Server | RPi / Embebido |

## Tecnologías

| Capa | Tecnología |
|------|-----------|
| Red IoT | Thread (IEEE 802.15.4) + 6LoWPAN + IPv6 |
| Aplicación IoT | CoAP + CBOR (binario compacto) |
| Gateway | OpenThread Border Router (Docker) |
| Plataforma IoT | ThingsBoard CE + Edge 4.2.0 |
| Bridge | Python (aiocoap + paho-mqtt) |
| Dashboard | FastAPI + Leaflet + Chart.js (Vanilla JS SPA) |
| Almacenamiento | PostgreSQL 16 |
| Contenedores | Docker Compose |

## Credenciales por defecto

| Sistema | URL | Usuario | Contraseña |
|---------|-----|---------|------------|
| ThingsBoard CE | `http://localhost:8080` | `tenant@thingsboard.org` | `tenant` |
| ThingsBoard Edge | `http://localhost:8082` | `tenant@thingsboard.org` | `tenant` |
| RPi "finca" | `ssh finca@10.182.112.114` | `finca` | `12345` |

## Cómo empezar

### 1. Levantar ThingsBoard CE (servidor central)

```bash
cd thingsboard-docker
docker compose up -d
# Esperar 1-2 min a que inicialice
```

### 2. Levantar el stack de campo (RPi o embebido)

```bash
# Si es la RPi existente:
cd otbr
docker compose up -d

# Si es un nuevo dispositivo embebido:
# Copiar sistema-embebido/ al dispositivo y ejecutar:
cd ~/sistema-embebido
sudo docker compose up -d
```

### 3. Conectar Edge con CE

- En ThingsBoard CE: **Edge Management → Edges → +** → crear Edge
- Copiar `CLOUD_ROUTING_KEY` y `CLOUD_ROUTING_SECRET`
- Pegarlos en el `docker-compose.yml` del Edge
- `CLOUD_RPC_HOST` debe apuntar a la IP del servidor CE

### 4. Abrir el dashboard

```bash
cd otbr/granja-dashboard
docker compose up -d -f  # o reconstruir si cambió
# Abrir http://localhost:3000
```

### 5. Flashear nodos ESP32-C6

```bash
cd nodo-coap-sed
. ~/.espressif/v5.5.4/esp-idf/export.sh
rm -f sdkconfig
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.sed" idf.py reconfigure
idf.py build
idf.py -p /dev/ttyACM1 flash
```
