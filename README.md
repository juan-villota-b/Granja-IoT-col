# Granja IoT

Sistema de agricultura de precisión con red **OpenThread**, nodos **ESP32-C6** y gateway **ThingsBoard Edge**.

## Arquitectura

```
┌─────────────────────────────────────────────────────────────────────┐
│                     THINGSBOARD CLOUD                              │
│                  (sync Edge via MQTT / RPC)                        │
└──────────────────────────┬──────────────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────────────┐
│                     RASPBERRY PI 4                                  │
│                                                                     │
│  ┌─────────────────┐   ┌──────────────────┐   ┌─────────────────┐  │
│  │  THINGSBOARD     │   │  BRIDGE (Python) │   │  OTBR            │  │
│  │  EDGE 4.2.0      │◄──│                  │◄──│  Border Router   │  │
│  │  (MQTT :1883)    │   │  main.py         │   │  (REST :8081)    │  │
│  │  (CoAP :5683)    │   │                  │   │                  │  │
│  │  (Web  :8082)    │   │  Descubre nodos  │   │  wpan0 ↔ wlan0   │  │
│  │                  │   │  Lee CoAP/CBOR   │   │  UART → RCP      │  │
│  │  PostgreSQL 16   │   │  Publica MQTT    │   │                  │  │
│  └─────────────────┘   └──────────────────┘   └────────┬─────────┘  │
└─────────────────────────────────────────────────────────┼────────────┘
                                                          │ UART
                                                          │ /dev/ttyACM0
┌─────────────────────────────────────────────────────────▼────────────┐
│                 RED THREAD (IEEE 802.15.4)                           │
│                 PAN 0x1234 — Canal 15 — "IOT-LAB-NET"               │
│                                                                     │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌──────────────┐  │
│  │ NODO-TH-01 │  │ NODO-TH-02 │  │ NODO-TH-03 │  │ NODO-TH-SED  │  │
│  │ ZONA-A     │  │ ZONA-A     │  │ ZONA-B     │  │ ZONA-B (SED) │  │
│  │ Router FTD │  │ Router FTD │  │ Router FTD │  │ Sleepy End   │  │
│  │ ESP32-C6   │  │ ESP32-C6   │  │ ESP32-C6   │  │ ESP32-C6     │  │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘  └───────┬──────┘  │
│        │               │               │                 │         │
│        └───────┬───────┴───────┬───────┴─────────┬───────┘         │
│                │               │                 │                 │
│        ┌───────▼───────────────▼─────────────────▼──────┐          │
│        │ NODO-VALVE-01 (ZONA-A, actuator de válvula)   │          │
│        └────────────────────────────────────────────────┘          │
└─────────────────────────────────────────────────────────────────────┘
```

## Nodos Thread

Cada nodo ESP32-C6 ejecuta un servidor **CoAP** con recursos CBOR:

| Recurso | Método | Payload |
|---------|--------|---------|
| `/env/temp` | GET | `{"t": 25.3}` |
| `/env/hum` | GET | `{"h": 60}` |
| `/act/valve` | GET/PUT | `{"v": 0}` |
| `/sys/health` | GET | `{"batt": 3100, "rssi": -65, "up": 12345}` |
| `/sys/info` | GET | Identidad del nodo |
| `/config/thresholds` | PUT | Config remota de umbrales |

| Nodo | Tipo | Zona | Rol |
|------|------|------|-----|
| NODO-TH-01 | Temp/Hum | ZONA-A | Router FTD |
| NODO-TH-02 | Temp/Hum | ZONA-A | Router FTD |
| NODO-TH-03 | Temp/Hum | ZONA-B | Router FTD |
| NODO-TH-SED-01 | Temp/Hum | ZONA-B | Sleepy End Device (bajo consumo) |
| NODO-VALVE-01 | Válvula | ZONA-A | Router FTD + actuador |

## Flujo de datos

1. **Nodos** miden temperatura/humedad y exponen recursos CoAP en la red Thread
2. **OTBR** (OpenThread Border Router) puentea Thread ↔ Wi-Fi, dando IPv6 a los nodos
3. **Bridge** (Python) descubre nodos (simulados o reales vía OTBR REST API), consulta `/env/temp`, `/env/hum` y `/sys/health` por CoAP con payload CBOR, y publica la telemetría a ThingsBoard Edge via MQTT Gateway API
4. **ThingsBoard Edge** almacena en PostgreSQL, sincroniza con ThingsBoard Cloud, y permite RPC (ej: `set_valve`) desde el dashboard hacia los nodos

## Estructura del proyecto

```
Granja-IOT/
├── Nodo-router-t-h/           # Firmware ESP32-C6 (ESP-IDF)
│   ├── main/
│   │   ├── node_config.h      # ⭐ Config única del nodo (ID, red Thread, umbrales)
│   │   ├── coap_server.c      # Servidor CoAP con recursos TH
│   │   ├── thread_launch.c    # Inicialización OpenThread
│   │   └── sensor.c           # Lectura de sensor (simulado)
│   ├── sdkconfig              # Config de compilación
│   └── sdkconfig.defaults     # Valores por defecto
├── Raspberry-pi-4/
│   ├── iot-gateway/           # Despliegue principal (Docker Compose)
│   │   └── docker-compose.yml # OTBR + ThingsBoard Edge + PostgreSQL
│   ├── bridge/                # Orquestador Python
│   │   ├── main.py            # Loop: descubre → lee CoAP → publica MQTT
│   │   ├── config.yaml        # Config: nodos simulados, MQTT credenciales
│   │   ├── coap/              # Cliente CoAP (aiocoap)
│   │   ├── mqtt/              # Publisher + Subscriber MQTT
│   │   ├── discovery/         # Escáner de nodos (OTBR REST API)
│   │   ├── downlink/          # Manejo de RPC desde TB Edge
│   │   └── simulation/        # Simulador de 5 nodos CoAP
│   ├── tb-edge/               # ThingsBoard Edge standalone
│   ├── otbr/                  # OTBR standalone
│   └── README.md
└── README.md
```

## Comandos rápidos

### Nodo firmware
```bash
cd Nodo-router-t-h
idf.py build flash monitor
```

### Gateway (Raspberry Pi)
```bash
cd Raspberry-pi-4/iot-gateway
docker compose up -d
```

### Bridge (simulación sin hardware)
```bash
cd Raspberry-pi-4/bridge
python3 main.py -c config.yaml
```
