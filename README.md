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

## Seguridad y estado de implementación

### Unión inicial de nodos (commissioning)

Actualmente cada nodo trae las credenciales de red Thread **quemadas en firme** en `node_config.h`:

| Parámetro | Ejemplo | Función |
|-----------|---------|---------|
| `THREAD_NETWORK_KEY` | `0011223344556677...` | Clave maestra AES-CCM de la red |
| `THREAD_PANID` | `0x1234` | ID de la red personal |
| `THREAD_PSKC` | `104810e2315100af...` | PSKc para commissioner externo |

Esto significa que **cualquiera con acceso al firmware** (código fuente o binario flasheado) tiene la clave de red y puede unirse. El flujo actual es:

1. Se edita `node_config.h` con los parámetros de la red
2. Se compila y flashea el nodo
3. Al encender, el nodo se une automáticamente con esas credenciales

### Seguridad actual

| Capa | Protección | Estado |
|------|-----------|--------|
| IEEE 802.15.4 (MAC) | AES-CCM cifra todas las tramas entre nodos | ✅ Implementado (nativo de Thread) |
| Enlace Thread | Clave de red compartida, rotación automática de clave | ✅ Implementado |
| CoAP | Sin cifrado — payload en texto plano o CBOR | ❌ Sin DTLS |
| Commissioning | Sin autenticación de dispositivo individual | ❌ Sin PKG ni Joiners |

### Lo que falta implementar

| Funcionalidad | Prioridad | Descripción |
|---------------|-----------|-------------|
| **Commissioning nativo Thread** | Alta | Usar el proceso estándar de Thread: un **Commissioner** (ejecutado en el OTBR) autentica cada **Joiner** antes de darle las credenciales de red. Así un nodo nuevo solo puede unirse si un administrador lo autoriza explícitamente. |
| **Autenticación por dispositivo** | Alta | Asignar a cada nodo un **Joiner ID** único (derivado de su EUI-64). El Commissioner valida contra una lista de dispositivos autorizados antes de compartir la network key. |
| **DTLS (CoAPS)** | Alta | Implementar **CoAP sobre DTLS** con PSK para que la telemetría y los comandos viajen cifrados entre el nodo y el bridge, y no puedan ser interceptados en la red local. |
| **Flash Encryption** | Media | Activar **Flash Encryption** en el ESP32-C6 para que la network key y otros secretos no se puedan leer extrayendo el binario del chip. |
| **Secure Boot** | Media | Activar **Secure Boot V2** para garantizar que solo firmware firmado ejecute en los nodos, evitando que un atacante flashee un binario modificado. |
| **NVS Encryption** | Media | Cifrar la partición NVS donde se almacenan credenciales y configuración persistente. |
| **Rotación periódica de network key** | Baja | Implementar cambio automático de la clave de red Thread sin interrumpir la comunicación, limitando el daño si una clave se expone. |
| **Whitelist MAC en OTBR** | Baja | Configurar el OTBR para solo aceptar dispositivos con EUI-64 conocido, añadiendo una capa extra de control de acceso. |

### Resumen de riesgos actuales

En el estado actual, cualquiera que obtenga el firmware (por acceso al repo, al binario flasheado, o a un cable UART) puede:
- Extraer la **network key** y unirse a la red Thread
- Enviar comandos CoAP a cualquier nodo (no hay autenticación ni cifrado en la aplicación)
- Flashear su propio nodo malicioso en la red

Para un MVP/Laboratorio esto es aceptable, pero para un despliegue productivo en campo se deben implementar las mejoras marcadas como **prioridad alta** antes de conectar nodos reales en un entorno no controlado.

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
