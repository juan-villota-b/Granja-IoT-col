# Raspberry Pi — Gateway IoT (Finca)

## Especificaciones del sistema

| Componente | Detalle |
|------------|---------|
| **Modelo** | Raspberry Pi 4 Model B Rev 1.2 |
| **SO** | Debian GNU/Linux 13 (trixie) |
| **Kernel** | 6.12.75+rpt-rpi-v8 (aarch64) |
| **Hostname** | `finca` |
| **IP** | `10.182.112.114/24` (WiFi wlan0) |
| **RAM** | 3.7 GB |
| **Almacenamiento** | 29 GB microSD (14 GB usados) |
| **Usuario** | `finca` (uid=1000, grupos: sudo, docker, dialout, gpio, spi, i2c) |
| **Docker** | v29.4.2, Compose v5.1.3 |
| **Python** | 3.13.5 |

## Paquetes Python clave

| Paquete | Versión | Uso |
|---------|---------|-----|
| `aiocoap` | 0.4.17 | Servidor/cliente CoAP para simular nodos |
| `cbor2` | 6.1.1 | Codificación CBOR de payloads CoAP |
| `paho-mqtt` | 2.1.0 | Cliente MQTT para conectar con ThingsBoard Edge |
| `PyYAML` | 6.0.2 | Parseo de config.yaml |
| `requests` | 2.32.3 | Llamadas REST API a ThingsBoard |
| `pyserial` | 3.5 | Comunicación serial (RCP, debug) |

## Estructura de directorios

```
/home/finca/
├── iot-gateway/          ← Gateway principal (Docker Compose)
│   ├── docker-compose.yml    OTBR + TB Edge + PostgreSQL
│   ├── otbr/                 Configuración del RCP
│   │   └── otbr-env.list
│   └── tb-edge/              (reservado para config extra)
│
├── bridge/               ← Bridge Python (simulación + gateway MQTT)
│   ├── main.py               Orquestador principal
│   ├── config.yaml           Configuración general
│   ├── requirements.txt      Dependencias Python
│   ├── simulation/
│   │   ├── node_sim.py       Simulador de nodo ESP32-C6 (CoAP)
│   │   └── network_sim.py    Lanzador de 5 nodos simulados
│   ├── mqtt/
│   │   ├── publisher.py      Publica telemetría a TB Edge
│   │   └── subscriber.py     Escucha RPC desde TB Edge
│   ├── coap/
│   │   ├── client.py         Cliente CoAP genérico
│   │   └── registration.py   Registro de nodos
│   ├── downlink/
│   │   └── handler.py        Manejo de comandos RPC (válvula)
│   ├── discovery/            Descubrimiento de nodos
│   └── models/               Modelos de datos
│
├── tb-edge/              ← ThingsBoard Edge standalone
│   └── docker-compose.yml
│
└── otbr/                 ← OTBR standalone
    ├── docker-compose.yml
    ├── listen.py
    └── thingsboard-send.py
```

---

## Explicación de cada archivo

### `iot-gateway/docker-compose.yml` (Gateway principal)

Define 3 servicios Docker que corren siempre en la Raspberry:

#### `otbr` — OpenThread Border Router

```
image: openthread/border-router:latest
network_mode: host
privileged: true
devices:
  - /dev/ttyACM0:/dev/ttyACM0
environment:
  OT_RCP_DEVICE: spinel+hdlc+uart:///dev/ttyACM0?uart-baudrate=1000000
  OT_INFRA_IF: wlan0
  OT_THREAD_IF: wpan0
```

- **Rol:** Puente entre la red Thread (IEEE 802.15.4) y WiFi/Ethernet.
- **RCP:** Se conecta via UART a 1 Mbps a un dongle 802.15.4 en `/dev/ttyACM0`.
- **Interfaces:** `wlan0` (infraestructura WiFi), `wpan0` (Thread).
- **Puertos expuestos:**
  - `8080` — Web GUI de OTBR
  - `8081` — REST API de OTBR

#### `mytbedge` — ThingsBoard Edge

```
image: thingsboard/tb-edge:4.2.0EDGE
ports:
  - "8082:8080"     # Web UI
  - "1883:1883"     # MQTT
  - "5683-5688:5683-5688/udp"  # CoAP
environment:
  SPRING_DATASOURCE_URL: jdbc:postgresql://postgres:5432/tb-edge
  CLOUD_RPC_HOST: 10.246.209.11
  CLOUD_RPC_PORT: 7070
  CLOUD_ROUTING_KEY: e889d9b7-...
  CLOUD_ROUTING_SECRET: lx1a7zc2k...
```

- **Rol:** Plataforma IoT local con dashboard, reglas y almacenamiento.
- **Puertos:**
  - `8082` → Web UI (login: `tenant@thingsboard.org` / `tenant`)
  - `1883` → MQTT para ingesta de datos
  - `5683-5688/udp` → CoAP
- **Cloud:** Conectado a un servidor ThingsBoard cloud en `10.246.209.11:7070`.

#### `postgres` — Base de datos

```
image: postgres:16
environment:
  POSTGRES_DB: tb-edge
  POSTGRES_PASSWORD: postgres
```

- Almacena toda la configuración y telemetría de TB Edge.
- Volumen persistente: `tb-edge-postgres-data`.

---

### `bridge/` — Bridge Python de simulación

#### `main.py` — Orquestador

- Lee `config.yaml`.
- Crea contexto CoAP (`aiocoap`).
- Inicia publisher MQTT y subscriber de RPC.
- Cada **2 segundos**:
  1. **Discovery:** Escanea puertos CoAP (15683–15687) para detectar nodos.
  2. **Conexión:** Publica `v1/gateway/connect` para cada nodo descubierto.
  3. **Lectura:** Hace GET CoAP a `/env/temp`, `/env/hum`, `/sys/health`.
  4. **Publicación:** Envía telemetría via `v1/gateway/telemetry` a TB Edge.
- Recibe RPC por MQTT y reenvía como PUT CoAP a los nodos.

#### `config.yaml` — Configuración general

```yaml
mqtt:
  host: localhost
  port: 1883
  username: MXGYfpiNFAhdNIiDtNB1   # Token del gateway device
  telemetry_topic: v1/gateway/telemetry
  connect_topic: v1/gateway/connect

simulation:
  enabled: true
  nodes:
    - node_id: NODO-TH-01
      coap_port: 15683
      type: TH
      zone: ZONA-A
    - node_id: NODO-TH-02 ...
    - node_id: NODO-TH-03 ...
    - node_id: NODO-TH-SED-01 ...
    - node_id: NODO-VALVE-01 ...
```

#### `simulation/node_sim.py` — Simulador de nodo ESP32-C6

Cada instancia es un servidor CoAP con estos recursos:

| Recurso CoAP | Método | Payload CBOR | Descripción |
|-------------|--------|-------------|-------------|
| `/env/temp` | GET | `{"t": float16}` | Temperatura (valor base + ruido gaussiano) |
| `/env/hum` | GET | `{"h": uint8}` | Humedad (0–100%) |
| `/act/valve` | GET / PUT | `{"v": 0/1}` | Estado de válvula (recibe comandos) |
| `/sys/health` | GET | `{"batt": uint16, "rssi": int8, "up": uint32}` | Batería (mV), RSSI (dBm), uptime (s) |
| `/sys/register` | PUT | `{"registered": bool}` | Registro del nodo |

Los valores tienen ruido gaussiano para simular lecturas reales.

#### `simulation/network_sim.py` — Lanzador de nodos

- Inicia 5 procesos `node_sim.py` como subprocesos.
- Usa `--base-port` para asignar puertos (default 15683).
- Cada nodo tiene valores base distintos (temp, hum) según su zona.

#### `mqtt/publisher.py` — Publicador MQTT

- Conecta a TB Edge vía MQTT en localhost:1883.
- Se autentica con el token del dispositivo gateway.
- Métodos:
  - `connect_dev(device_name, type)` → publica en `v1/gateway/connect`
  - `telemetry(device_name, ts, values)` → publica en `v1/gateway/telemetry`

#### `mqtt/subscriber.py` — Suscriptor RPC

- Se suscribe a `v1/gateway/rpc`.
- Al recibir un comando, llama al callback `_on_rpc` que reenvía al nodo CoAP.
- Comandos soportados: `set_valve` (abre/cierra válvula).

#### `downlink/handler.py` — Manejador de downlink

- Traduce comandos RPC de TB Edge a requests CoAP a los nodos.
- `set_valve` → `PUT /act/valve {"v": state}`

#### `coap/client.py` — Cliente CoAP genérico

- Funciones `get_cbor()` y `put_cbor()` para interactuar con nodos CoAP.

#### `coap/registration.py` — Registro de nodos

- Payload de registro que los nodos enviarían al iniciar.

---

### `tb-edge/docker-compose.yml` (standalone)

Versión independiente de ThingsBoard Edge + PostgreSQL (sin OTBR). Útil para probar TB Edge sin la red Thread.

### `otbr/` (standalone)

Versión independiente del Border Router con scripts auxiliares:
- `listen.py` — Escucha eventos de la red Thread.
- `thingsboard-send.py` — Envía datos a ThingsBoard desde Thread.

---

## Puertos expuestos (Raspberry Pi)

| Puerto | Servicio | Descripción |
|--------|----------|-------------|
| 8082 | TB Edge Web UI | Dashboard, dispositivos, reglas |
| 1883 | TB Edge MQTT | Ingesta de telemetría |
| 5683-5688/udp | TB Edge CoAP | API CoAP |
| 8080 | OTBR Web GUI | Monitoreo de red Thread |
| 8081 | OTBR REST API | Control de red Thread |

---

## Flujo de datos actual

```
[Simulación CoAP]              [Bridge Python]                [ThingsBoard Edge]
NODO-TH-01 (p.15683) ──GET──→ main.py ──MQTT──→ mytbedge (puerto 1883)
NODO-TH-02 (p.15684) ──GET──→ main.py ──MQTT──→ mytbedge
NODO-TH-03 (p.15685) ──GET──→ main.py ──MQTT──→ mytbedge
NODO-TH-SED-01 (p.15686) ─GET→ main.py ──MQTT──→ mytbedge
NODO-VALVE-01 (p.15687) ─GET→ main.py ──MQTT──→ mytbedge
                                │
                           [TB Edge Cloud]
                          10.246.209.11:7070
```

1. Los 5 nodos simulados exponen recursos CoAP en localhost.
2. `main.py` hace polling cada ~5s a cada nodo.
3. Los datos se publican por MQTT al gateway device `IoT-Gateway`.
4. TB Edge almacena en PostgreSQL y replica al cloud.

---

## Cómo conectar ESP32-C6 reales

Para migrar de la simulación a hardware real, se necesita:

### Hardware

| Componente | Cantidad | Detalle |
|------------|----------|---------|
| **ESP32-C6** | 1+ | Microcontrolador con radio Thread 802.15.4 |
| **Dongle RCP** | 1 | nRF52840 o ESP32-C6 con firmware `ot-rcp` |
| **Sensor DHT22/BME280** | 1 por nodo | Temperatura y humedad |
| **Relé + válvula solenoide** | 1 | Para control de riego (opcional) |
| **Batería 2×AA + regulador** | 1 por nodo | Alimentación para nodos SED |
| **USB-UART (CP2102)** | 1 | Depuración/programación |

### Configuración de red Thread

El OTBR ya está configurado con:
- **Canal:** 26
- **RCP:** `/dev/ttyACM0` a 1 Mbps
- **Infra interfaz:** `wlan0`
- **PAN:** Valor por defecto de OpenThread
- **NAT64:** Activo (`192.168.255.0/24`)

Los ESP32-C6 deben:
1. Flashear con OpenThread (ESP-IDF + OpenThread).
2. Configurar RCP o SED según el rol.
3. Unirse a la red Thread del OTBR.
4. Implementar servidor CoAP con los mismos recursos que `node_sim.py`:
   - `GET /env/temp` → CBOR `{"t": float16}`
   - `GET /env/hum` → CBOR `{"h": uint8}`
   - `GET /act/valve` y `PUT /act/valve` → CBOR `{"v": 0/1}`
   - `GET /sys/health` → CBOR `{"batt": uint16, "rssi": int8, "up": uint32}`

### Cambios en el bridge

Al tener nodos reales en la red Thread, hay que:

1. **Deshabilitar simulación** en `config.yaml`:
   ```yaml
   simulation:
     enabled: false
   ```

2. **Habilitar OTBR discovery:**
   ```yaml
   otbr:
     enabled: true
     rest_api: http://localhost:8081
   ```

3. El bridge entonces usará la REST API del OTBR (`localhost:8081`) para descubrir los nodos Thread via su IPv6, en vez de los puertos simulados en `::1`.

### API CoAP de los nodos (contrato inamovible)

| Campo | Valor |
|-------|-------|
| Recurso | `/env/temp`, `/env/hum`, `/act/valve`, `/sys/health` |
| Transporte | CoAP/UDP 5683 |
| Content-Format | `60` (`application/cbor`) |
| Payload temp | `A1 61 74 F9 hh ll` (6 bytes, float16) |
| Payload hum | `A1 61 68 18 hh` (4 bytes, uint8) |
| Payload valve | `A1 61 76 0X` (4 bytes, bool) |

### Bajo consumo (SED)

Para nodos a batería (tipo `NODO-TH-SED-01`):
- Usar **Sleepy End Device** (SED) de Thread.
- Poll period: 5 s (balance latencia/batería).
- Deep sleep entre lecturas (~5 µA).
- Usar CoAP Observe para push-on-change.
- Reportar health cada 10 lecturas como piggyback.
