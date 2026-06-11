# Nodo-sensor-SEDv2

Firmware para ESP32-C6 como nodo sensor Sleepy End Device (SED) en red Thread.
Lee temperatura, la envía por CoAP POST al Bridge OTBR, que la publica en ThingsBoard.

## Índice

- [Arquitectura general](#arquitectura-general)
- [Estructura de archivos](#estructura-de-archivos)
- [Flujo de datos](#flujo-de-datos)
- [Estrategia de bajo consumo](#estrategia-de-bajo-consumo)
- [Estrategia de comunicación CoAP](#estrategia-de-comunicación-coap)
- [Temporización y tiempos](#temporización-y-tiempos)
- [Formato de payload CBOR](#formato-de-payload-cbor)
- [Configuración remota (downlink)](#configuración-remota-downlink)
- [Configuración SED (Sleepy End Device)](#configuración-sed-sleepy-end-device)
- [sdkconfig.defaults](#sdkconfigdefaults)
- [Bridge (lado servidor)](#bridge-lado-servidor)
- [Respuesta a preguntas frecuentes](#respuesta-a-preguntas-frecuentes)

---

## Arquitectura general

```
┌────────────────────────────────────────────────────────────────────┐
│                      Nodo-sensor-SEDv2                             │
│  ESP32-C6                                                         │
│                                                                   │
│  ┌──────────────┐  ┌──────────────────┐  ┌────────────────────┐  │
│  │ sensor_temp  │→│  nodo_th_auto    │→│  push_client       │  │
│  │  .c/.h       │  │  (orquestador)  │  │  (CoAP POST)      │  │
│  └──────────────┘  └──────────────────┘  └─────────┬──────────┘  │
│                                                     │             │
│  ┌──────────────┐  ┌──────────────────┐             │             │
│  │ config       │  │  OpenThread MTD  │             │             │
│  │ .c/.h        │  │  (SED + poll)    │             │             │
│  └──────────────┘  └──────────────────┘             │             │
│                                                     ▼             │
│                                          Thread 802.15.4         │
└────────────────────────────────────────────────────────────────────┘
                           │
                     CoAP CON POST /readings
                     CBOR {id, t, r, u}
                           │
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  Bridge OTBR (iot-bridge)                                         │
│  aiocoap :5685 → MQTTPublisher → ThingsBoard Edge MQTT :1884      │
│  auto-registro + downlink piggyback en respuesta                   │
└────────────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  ThingsBoard Edge → ThingsBoard CE → Dashboard (Granja) :3000     │
└────────────────────────────────────────────────────────────────────┘
```

---

## Estructura de archivos

```
Nodo-sensor-SEDv2/
├── CMakeLists.txt              ← Proyecto ESP-IDF (Minimal Build)
├── partitions.csv              ← Tabla de particiones (nvs + phy + factory)
├── sdkconfig.defaults          ← Configuración por defecto del build
├── sdkconfig                   ← Generado por idf.py (no editar)
├── dependencies.lock           ← Generado (gestión de componentes)
├── .gitignore
├── .clangd
├── .devcontainer/
└── main/
    ├── CMakeLists.txt          ← Registro de fuentes y dependencias
    ├── node_config.h            ← Identidad y constantes del nodo
    ├── config.h                ← Struct de configuración persistente
    ├── config.c                ← Carga/guarda configuración en NVS
    ├── sensor_temp.h           ← Interfaz del sensor de temperatura
    ├── sensor_temp.c           ← Implementación (simulada o real)
    ├── push_client.h           ← Interfaz de envío CoAP
    ├── push_client.c           ← POST /readings al Bridge
    ├── nodo_th_auto.c          ← Orquestador principal (app_main)
    ├── esp_ot_config.h         ← Configuración OpenThread (ESP-IDF)
    └── esp_ot_custom_config.h  ← Config personalizada OpenThread (ESP-IDF)
```

### Descripción por archivo

#### `CMakeLists.txt` (raíz)
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(nodo_sed_v2)
```
- `MINIMAL_BUILD ON`: elimina componentes no usados (WiFi, Bluetooth, etc.)
- Proyecto compilado para `esp32c6`

#### `main/CMakeLists.txt`
```cmake
idf_component_register(SRCS nodo_th_auto.c push_client.c config.c sensor_temp.c
                       INCLUDE_DIRS "."
                       REQUIRES esp_event esp_netif nvs_flash openthread vfs coap)
```
Dependencias: OpenThread, CoAP (libcoap), NVS, VFS (eventfd), event loop.

#### `node_config.h`
Define la identidad del nodo y constantes de red:
| Símbolo | Valor | Significado |
|---|---|---|
| `NODE_ID` | `"NODO-SENSOR-1"` | Identificador único en ThingsBoard |
| `ZONE_ID` | `"ZONA-A"` | Zona geográfica |
| `NODE_TYPE` | `"sensor_sed"` | Tipo de dispositivo |
| `FW_VERSION` | `"5.2.0"` | Versión de firmware |
| `TEMP_THRESHOLD_C_DEFAULT` | `0.5°C` | Umbral de temperatura para push |
| `HEARTBEAT_S_DEFAULT` | `300s` | Máximo sin push |
| `SAMPLE_INTERVAL_MS` | `30000ms` | Intervalo entre ciclos |
| `BRIDGE_IPV6` | `fd29:…:fc00` | Dirección IPv6 del Bridge (ALOC) |
| `BRIDGE_PORT` | `5685` | Puerto CoAP del Bridge |
| `REGISTER_MAX_RETRIES` | `3` | Reintentos del primer push |
| `REGISTER_RETRY_MS` | `1500ms` | Timeout por intento |

#### `config.h` / `config.c`
Estructura de configuración persistente en NVS:
```c
typedef struct {
    float    temp_threshold_c;   // umbral temperatura
    uint16_t heartbeat_s;        // máximo sin push
    uint32_t sample_interval_ms; // tiempo entre ciclos
    float    lat, lng;           // coordenadas GPS
} nodo_config_t;
```
- `config_init()`: carga desde NVS, o usa defaults
- `config_save()`: persiste en NVS (usado en futuro downlink)
- Claves NVS: `temp_th`, `hb_s`, `sample_ms`, `lat_i`, `lng_i`
- Compatible con SED-2 (mismas claves)

#### `sensor_temp.h` / `sensor_temp.c`
Abstracción del sensor de temperatura:
```c
typedef struct { float temperatura_c; } sensor_temp_t;
void sensor_temp_init(void);
sensor_temp_t sensor_temp_leer(void);
```
- Simulación senoidal: `T = 25 + 2·sin(2π·t/1200) + ruido(±0.1°C)`
- Período: 1200s (20 minutos)
- Rango: ~23°C a ~27°C
- Para reemplazar con sensor real: solo cambiar `sensor_temp_leer()`

#### `push_client.h` / `push_client.c`
Cliente CoAP para POST /readings:
```c
esp_err_t push_telemetry(sensor_temp_t *lectura, int8_t rssi, uint32_t uptime_s, bool is_first);
```
- `is_first=true`: envía `{id, tp, v, t, r, u}` (registro + telemetría)
- `is_first=false`: envía `{id, t, r, u}` (solo telemetría)
- CON message, response handler que detecta ACK 2.xx
- RSSI real desde `otThreadGetParentLastRssi()`
- Timeout por intento: 1500ms
- Reintentos: 3 (primero) / 2 (subsiguientes)

#### `nodo_th_auto.c`
Orquestador principal. `app_main()`:
1. Init: NVS, event loop, netif, OpenThread, config, sensor
2. Configura modo SED (rx-on-when-idle=0) **antes** de habilitar Thread
3. Espera 4s a que Thread se una a la red
4. Verifica modo SED post-join (lo fuerza si NVS persiste rx=1)
5. 1er push CON con reintentos (registro + telemetría)
6. Loop infinito:
   - Lee sensor
   - Si ΔT > umbral o heartbeat → push
   - `vTaskDelay(30000)` → CPU entra en light sleep automático

#### `esp_ot_config.h` / `esp_ot_custom_config.h`
Headers de configuración de OpenThread para ESP-IDF.
- `esp_ot_config.h`: incluye `esp_openthread_types.h` y define helpers
- `esp_ot_custom_config.h`: personalizaciones (vacío, usa defaults)

---

## Flujo de datos

### Uplink (nodo → dashboard)

```
sensor_temp_leer()
    │ T = 25.3°C
    ▼
nodo_th_auto.c: ΔT > 0.5°C? ¿o heartbeat 300s?
    │ sí
    ▼
push_telemetry(lectura, 0, uptime, false)
    │
    ├─ get_rssi() → -62 dBm
    ├─ encode_cbor_push() → CBOR {id, t, r, u} = ~25 bytes
    ├─ coap_new_context() + coap_new_client_session()
    ├─ coap_send() → CON POST /readings a [fd29::fc00]:5685
    │
    ▼  (Thread mesh → OTBR wpan0 → kernel)
    │
Bridge render_post():
    ├─ cbor2.loads() → {"id":"NODO-SENSOR-1","t":25.3,"r":-62,"u":1234}
    ├─ ¿nodo conocido? no → auto-register: connect_dev + attributes
    ├─ ¿"t" in data? sí → telemetry(temperature, rssi, uptime)
    ├─ ¿pending_commands? → devuelve en respuesta
    └─ 2.05 Content {comando o vacío}
    │
    ▼  (MQTT)
    │
ThingsBoard Edge → CE → Dashboard (Granja)
```

### Downlink (dashboard → nodo)

```
Dashboard → RPC "set_thresholds" (TB CE → TB Edge)
    │
    ▼
Bridge MQTTSubscriber._on_rpc()
    ├─ pending_commands["NODO-SENSOR-1"] = {"tt":1.0,"hb":120}
    │
    ▼  (espera próximo POST del nodo)
    │
Bridge render_post() → pop pending_commands
    └─ 2.05 Content {t}   ───┐
                              │ CoAP response
Nodo push_handler()           │
    └─ (actualmente solo      │
       detecta ACK, no        │
       parsea payload)  ◄─────┘
```

> **Nota**: El parsing del payload de respuesta downlink está pendiente de implementar.

---

## Estrategia de bajo consumo

Tres niveles de ahorro, **Thread siempre conectado**:

### 1. Modo SED (Sleepy End Device)

| Parámetro | Configuración |
|---|---|
| `mRxOnWhenIdle` | `false` (radio apagado entre polls) |
| `mDeviceType` | `false` (MTD, no FTD) |
| Poll period | `30000ms` (un poll cada 30s) |

El radio 802.15.4 está apagado ~99.997% del tiempo.
Despierta 1ms cada 30s para preguntar al padre si hay datos pendientes.

### 2. PM + Tickless Idle

| Config | Efecto |
|---|---|
| `CONFIG_PM_ENABLE=y` | Power management activado |
| `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y` | CPU entra en light sleep durante `vTaskDelay` |

Durante `vTaskDelay(30000)`, la CPU entra en light sleep automático.
El stack de OpenThread coordina el sueño con el hardware (señal `esp_openthread_sleep`).
Thread **no se desconecta** porque OT maneja el sleep/resume.

### 3. 80MHz + MTD

| Config | Efecto |
|---|---|
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_80=y` | CPU a 80MHz (vs 160MHz) |
| `CONFIG_OPENTHREAD_MTD=y` | Minimal Thread Device (sin routing) |

### Consumo estimado

| Estado | Consumo | % del ciclo |
|---|---|---|
| Push activo (500ms) | ~35mA | 1.7% |
| CPU light sleep + radio SED | ~131µA | 98.3% |
| **Promedio** | **~0.7mA** | |

> **Medido sin cable USB.** Con USB conectado, el PHY USB agrega ~10mA fijos
> independientemente del firmware. El consumo total con USB es ~17mA.
> Para mediciones reales de batería, **desconectar el cable USB**.
>
> | Escenario | Consumo |
> |---|---|
> | USB conectado + console USB | ~17mA |
> | USB conectado + console UART (pines GPIO16/17) | ~7mA |
> | Sin USB (batería) | ~0.7-1mA |

---

## Estrategia de comunicación CoAP

### Tipo de mensaje

Siempre `COAP_MESSAGE_CON` (confirmable). El Bridge responde con ACK + 2.05 Content.
Esto permite:
- Detección confiable de entrega (ACK)
- Piggyback de comandos downlink en la respuesta

### Reintentos y timeouts

| Push | Timeout por intento | Reintentos | Tiempo máximo |
|---|---|---|---|
| 1er (registro) | **1500ms** | **3** | 4.5s |
| Subsecuentes | **1500ms** | **2** | 3.0s |

El timeout de 1500ms es necesario porque la sesión CoAP necesita establecerse
(creación de socket, resolución de ruta Thread, primer intento puede fallar).

### Mecanismo de ACK y reintentos

Cada intento sigue este flujo:

```
push_telemetry()
  │
  ├─ coap_register_response_handler(push_handler)
  │   Este callback se ejecuta cuando llega una respuesta CoAP.
  │   Si el código de respuesta es 2.xx, pone g_got_ack = true.
  │
  ├─ coap_send(sess, pdu)
  │   Envía CON POST /readings. El stack CoAP espera un ACK
  │   de nivel de transporte + respuesta 2.05 Content.
  │
  ├─ while (!g_got_ack && tiempo < timeout_ms)
  │   coap_io_process(ctx, 100)  // procesa I/O cada 100ms
  │   │
  │   ├─ Si llega respuesta → push_handler() → g_got_ack = true
  │   ├─ Si timeout → sale del while
  │   └─ Si timeout y aún hay reintentos → nuevo intento
  │
  ├─ coap_session_release(sess)
  ├─ coap_free_context(ctx)
  │
  ├─ ¿g_got_ack? → LOG "Push exitoso intento N" → return ESP_OK
  └─ ¿no ACK? → LOG "Push intento N/M sin ACK"
                └─ ¿quedan reintentos? → nuevo intento (nuevo context + session)
```

El handler `push_handler()` es una función estática que solo establece una bandera:

```c
static bool g_got_ack = false;

static coap_response_t push_handler(coap_session_t *s, const coap_pdu_t *sent,
                                    const coap_pdu_t *rcvd, const coap_mid_t mid)
{
    if (rcvd) {
        unsigned int cls = coap_pdu_get_code(rcvd) >> 5;
        if (cls == 2) {       // 2.xx Success
            g_got_ack = true;
            ESP_LOGI(TAG, "ACK 2.xx del Bridge");
        }
    }
    return COAP_RESPONSE_OK;
}
```

**Nota importante:** Cada reintento crea un NUEVO `coap_context_t` y `coap_session_t`.
El anterior se destruye. Esto es necesario porque libcoap no permite reusar
contextos para múltiples envíos en este patrón. El costo es ~2ms por contexto.

### ¿Por qué el primer intento de cada push suele fallar?

```
Intento 1:  coap_new_context() + coap_new_client_session()  → ~2ms
            coap_send()                                       → 0ms
            coap_io_process() esperando respuesta             → 1500ms timeout
            → SIN ACK (el socket UDP recién se estableció)
Intento 2:  coap_new_context() + coap_new_client_session()  → ~1ms (ruta caliente)
            coap_send()                                       → 0ms
            coap_io_process() esperando respuesta             → 50ms
            → ACK recibido ✅
```

El primer intento falla porque el socket UDP necesita negociar la ruta
en la red Thread (resolución de vecinos, 6LoWPAN). El segundo intento
aprovecha la caché de ruta y responde en ~50ms.

### ¿Por qué se ve duplicado en los logs del Bridge?

```
Bridge log:
  05:05:01 >>> NODO-SENSOR-1 t=25.0°C rssi=-64 up=4s    ← intento 1 (timeout)
  05:05:02 >>> NODO-SENSOR-1 t=25.0°C rssi=-64 up=4s    ← intento 2 (ACK ✅)
```

El Bridge procesa ambos POSTs como mensajes distintos.
No hay detección de duplicados porque cada intento tiene un `msg_id` diferente.
ThingsBoard maneja la deduplicación por timestamp, no por payload.
Esto no afecta el dashboard porque el segundo POST sobrescribe
el mismo timestamp en la serie temporal.

### Trigger de push

Solo se envía push si:
1. `|ΔT| > temp_threshold_c` (default 0.5°C) — cambio significativo
2. `uptime - last_push > heartbeat_s` (default 300s) — keepalive

Si no hay cambio, el ciclo se salta el push y solo duerme.

---

## Temporización y tiempos

### Ciclo completo (sin push)

```
t=0       Leer sensor (1ms)
t=0.001   ¿ΔT > umbral? No
t=0.002   vTaskDelay(29998ms) → CPU light sleep
t=30.000  Wake → repetir
```

### Ciclo con push

```
t=0       Leer sensor (1ms)
t=0.001   ¿ΔT > umbral? Sí
t=0.002   push_telemetry()
t=0.003   coap_new_context + session (2ms)
t=0.005   coap_send() → intento 1
t=1.505   Timeout intento 1, sin ACK
t=1.506   coap_new_context + session (2ms)
t=1.508   coap_send() → intento 2
t=1.558   ACK recibido (50ms después)
t=1.559   Push exitoso
t=1.560   vTaskDelay(29998ms) → CPU light sleep
t=31.558  Wake → repetir
```

### Heartbeat (push cada 300s sin cambios)

El heartbeat fuerza un push aunque la temperatura no haya cambiado.
Esto permite al Bridge saber que el nodo sigue vivo.
ThingsBoard recibe el heartbeat como telemetría normal.

---

## Formato de payload CBOR

### Primer push (registro + telemetría)

```
map(6) {
  "id":  "NODO-SENSOR-1"     → identificación
  "tp":  "sensor_sed"        → tipo de nodo
  "v":   "5.2.0"             → versión firmware
  "t":   25.3                 → temperatura (float16, 2 bytes)
  "r":   -62                  → RSSI (negint8, 1 byte)
  "u":   1234                 → uptime segundos (uint32, 5 bytes)
}
Total: ~32 bytes CBOR
```

### Push subsecuente (solo telemetría)

```
map(4) {
  "id":  "NODO-SENSOR-1"     → identificación (siempre incluido)
  "t":   25.3                 → temperatura (float16)
  "r":   -62                  → RSSI (negint8)
  "u":   1300                 → uptime segundos (uint32)
}
Total: ~25 bytes CBOR
```

El Bridge requiere `id` en todos los mensajes para auto-registro.
Sin `id`, responde `4.00 Bad Request`.

---

## Configuración remota (downlink)

El Bridge puede enviar comandos al nodo en la respuesta CoAP.

### Comandos disponibles

| Comando | Payload CBOR | Efecto esperado |
|---|---|---|
| `set_thresholds` | `{"tt":1.0,"hb":120}` | Cambiar umbral T° y heartbeat |
| `set_valve` | `{"v":1}` | Actuador (no implementado) |

### Flujo

1. Dashboard envía RPC a ThingsBoard CE
2. TB CE → TB Edge → Bridge (MQTT subscriber)
3. Bridge guarda en `pending_commands[nid]`
4. Siguiente POST del nodo → Bridge responde con comando
5. Nodo recibe respuesta, parsea payload, aplica cambios

> **Estado actual**: El nodo detecta ACK 2.xx pero no parsea el payload de la respuesta.
> El parsing del downlink está pendiente de implementar en `push_handler()`.

---

## Configuración SED (Sleepy End Device)

### ¿Por qué configurar el modo manualmente?

OpenThread guarda el `LinkMode` en NVS. Incluso tras `ot factoryreset`,
el flag `rx-on-when-idle` puede persistir. Si `rx-on-when-idle=1`,
el radio nunca duerme y el consumo es ~25mA continuos.

### Verificación

En el log de boot:
```
SED mode: rx=0 devtype=0 netdata=1
```

Si `rx=1`:
1. El modo no se aplicó correctamente
2. Verificar con `ot> mode` (requiere habilitar OpenThread CLI)
3. Ejecutar `mode -r` para quitar rx-on-when-idle

### Cómo se configura en código

En `join_thread_network()` (antes de `otThreadSetEnabled`):
```c
otLinkModeConfig sed_mode;
memset(&sed_mode, 0, sizeof(sed_mode));
sed_mode.mNetworkData = 1;  // solo bit de network data
otThreadSetLinkMode(ot, sed_mode);
```

Y luego del join (por si NVS lo sobrescribe):
```c
mode = otThreadGetLinkMode(ot);
if (mode.mRxOnWhenIdle) {
    mode.mRxOnWhenIdle = false;
    mode.mDeviceType = false;
    otThreadSetLinkMode(ot, mode);
}
```

---

## sdkconfig.defaults

```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000
CONFIG_PARTITION_TABLE_MD5=y

CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_80=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=6144

CONFIG_OPENTHREAD_ENABLED=y
CONFIG_OPENTHREAD_MTD=y
CONFIG_OPENTHREAD_DNS64_CLIENT=y
CONFIG_OPENTHREAD_TASK_SIZE=10240
CONFIG_OPENTHREAD_CLI=n
CONFIG_OPENTHREAD_HEADER_CUSTOM=y
CONFIG_OPENTHREAD_CUSTOM_HEADER_PATH="main"
CONFIG_OPENTHREAD_CUSTOM_HEADER_FILE_NAME="esp_ot_custom_config.h"

CONFIG_MBEDTLS_CMAC_C=y
CONFIG_MBEDTLS_SSL_PROTO_DTLS=y
CONFIG_MBEDTLS_KEY_EXCHANGE_ECJPAKE=y
CONFIG_MBEDTLS_ECJPAKE_C=y

CONFIG_PM_ENABLE=y
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y
CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP=200
CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y

CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=4096
CONFIG_LWIP_IPV6_NUM_ADDRESSES=8
CONFIG_LWIP_MULTICAST_PING=y
CONFIG_LWIP_HOOK_IP6_SELECT_SRC_ADDR_CUSTOM=y
```

---

## Bridge (lado servidor)

El Bridge corre en Docker (`otbr/bridge/`) en `network_mode: host`.
Usa `aiocoap` como servidor CoAP en puerto 5685/udp.

### Endpoint `/readings` (POST)

Recibe CBOR, procesa:

1. Extrae `id` del payload
2. Si nodo no está en `self._bridge.nodes` → auto-registro:
   - Crea entrada en `nodes[nid]` con IPv6, zona, tipo
   - Publica `v1/gateway/connect` MQTT
   - Publica `v1/gateway/attributes` MQTT
3. Publica telemetría: `temperature` (float), `rssi` (int), `uptime` (int)
4. Verifica `pending_commands[nid]`
5. Responde 2.05 Content con comando CBOR (o `{}` vacío)
6. Log: `>>> NODO-SENSOR-1 t=25.3°C rssi=-62 up=1234s`

### Comunicación con ThingsBoard

- `mqtt_mode: direct` — cada dispositivo usa su propio token MQTT
- Tokens definidos en `config.yaml` → `device_tokens`
- Tópicos: `v1/devices/me/telemetry`, `v1/gateway/connect`, `v1/gateway/attributes`
- RPC del dashboard entran por `v1/gateway/rpc`

### iptables

```bash
docker exec otbr ip6tables -I INPUT 1 -p udp --dport 5685 -j ACCEPT
```

Necesario porque el INPUT chain por defecto es `DROP`.
Sin esta regla, los paquetes CoAP de Thread no llegan al Bridge.

---

## Respuesta a preguntas frecuentes

### ¿Por qué no usar Observe (RFC 7641)?

Observe requiere que el **Bridge se suscriba** a recursos del nodo.
El Bridge OTBR **no tiene cliente CoAP** (solo servidor).
Además, Observe obliga al nodo a estar despierto para aceptar suscripciones,
lo que contradice la estrategia de bajo consumo.

### ¿Por qué no usar deep sleep?

El deep sleep desconecta Thread. Reconectar toma 3-5s con picos de 50mA,
anulando el ahorro para intervalos de 30s. Para intervalos >5 minutos,
deep sleep + rejoin puede ser beneficioso.

### ¿Por qué el push a veces se duplica en los logs del Bridge?

El reintento CoAP (2 intentos × 1500ms) puede generar dos POSTs
si el ACK del primero no llega a tiempo. ThingsBoard maneja duplicados
por timestamp, no por payload.

### ¿Por qué el primer intento siempre falla?

El `coap_new_context()` + `coap_new_client_session()` necesita
establecer el socket UDP en la interfaz Thread. El primer intento
suele exceder el timeout de 1500ms. El segundo intento aprovecha
la ruta ya establecida y responde en ~50ms.

### ¿Cuánto dura una batería?

Con consumo promedio ~0.7mA:
- Batería LiPo 500mAh → ~700 horas (~29 días)
- Batería LiPo 2000mAh → ~2857 horas (~119 días)
- 2 pilas AA (2000mAh) → ~119 días

Sin USB conectado.

### ¿El USB afecta el consumo?

Sí. El PHY USB del ESP32-C6 consume ~10mA independientemente del
estado del CPU. Para mediciones reales de batería, desconectar USB.
