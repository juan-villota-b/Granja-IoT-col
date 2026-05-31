# Nodo Router T/H — Firmware para ESP32-C6

Nodo sensor de temperatura y humedad sobre red **Thread**, con unión headless, telemetría CoAP/CBOR y bajo consumo para operación con batería.

---

## Índice

- [Arquitectura del sistema](#arquitectura-del-sistema)
- [Estructura del proyecto](#estructura-del-proyecto)
- [Flujo de ejecución](#flujo-de-ejecución)
- [Unión a red Thread (headless)](#unión-a-red-thread-headless)
- [Servidor CoAP](#servidor-coap)
  - [Recursos expuestos](#recursos-expuestos)
  - [Estrategia CON vs NON](#estrategia-con-vs-non)
  - [Formatos CBOR](#formatos-cbor)
- [Telemetría](#telemetría)
  - [Umbral + Heartbeat](#umbral--heartbeat)
  - [Escalabilidad del formato](#escalabilidad-del-formato)
- [Registro de nodos](#registro-de-nodos)
- [Gestión de energía](#gestión-de-energía)
  - [Modo batería](#modo-batería)
  - [Consumo estimado](#consumo-estimado)
- [Comunicación gateway ↔ nodo](#comunicación-gateway--nodo)
- [Cómo agregar un nuevo nodo](#cómo-agregar-un-nuevo-nodo)
- [Dependencias](#dependencias)

---

## Arquitectura del sistema

```
                    ┌─────────────────────────────────────────┐
                    │           Gateway (OTBR + Backend)        │
                    │  CoAP Client                             │
                    │  Descubre nodos via Thread               │
                    │  Consulta /sys/info al detectar nodo     │
                    │  Subscripción Observe a /env/temp y /hum │
                    └──────────────┬──────────────────────────┘
                                   │ Thread (IPv6 / 6LoWPAN)
                                   │ IEEE 802.15.4 canal 15
                                   │
                    ┌──────────────▼──────────────────────────┐
                    │         Nodo Router T/H (ESP32-C6)       │
                    │                                         │
                    │  CoAP Server (UDP/5683)                  │
                    │  ├─ /env/temp  (GET + Observe → NON)    │
                    │  ├─ /env/hum   (GET + Observe → NON)    │
                    │  ├─ /sys/info  (GET → CON)              │
                    │  └─ /sys/health(GET → CON)              │
                    │                                         │
                    │  Thread: FTD (Full Thread Device)        │
                    │  Radio: 802.15.4 nativa ESP32-C6         │
                    │  Unión: automática (headless)            │
                    │  Alimentación: batería 3.7V             │
                    └─────────────────────────────────────────┘
```

### Stack de protocolos

```
Aplicación     CoAP/UDP + CBOR
Red            IPv6 / 6LoWPAN / Thread
Enlace         IEEE 802.15.4 (O-QPSK, DSSS)
Físico         Radio nativa ESP32-C6 (canal 15)
```

---

## Estructura del proyecto

```
Nodo-router-t-h/
├── CMakeLists.txt                  # Proyecto ESP-IDF
├── partitions.csv                  # Tabla de particiones (NVS + factory)
├── sdkconfig.defaults              # Configuración por defecto del build
├── README.md                       # Este documento
└── main/
    ├── CMakeLists.txt              # Registro de componentes del módulo
    ├── idf_component.yml           # Dependencias del componente manager
    ├── app_main.c                  # Punto de entrada (sin CLI)
    ├── node_config.h               # ⭐ Único archivo que edita el usuario
    ├── thread_auto_join.c          # Unión headless a red Thread
    ├── thread_auto_join.h          # API de thread_auto_join
    ├── sensor_sim.c                # Sensor de T/H simulado
    ├── sensor_sim.h                # API del sensor (interfaz para reemplazar)
    ├── coap_handlers.c             # Servidor CoAP + recursos + notificaciones
    ├── coap_handlers.h             # API del servidor CoAP
    ├── registration.c              # Exposición de identidad del nodo
    ├── registration.h              # API de registro
    ├── power_mgmt.c                # Gestión de energía (DFS + light sleep)
    ├── power_mgmt.h                # API de power_mgmt
    ├── esp_ot_config.h             # Configuración de radio (nativa 802.15.4)
    └── esp_ot_custom_config.h      # Overrides de OpenThread (Observe API)
```

### Descripción de cada archivo

#### `app_main.c` — Punto de entrada

Función `app_main()` ejecutada por FreeRTOS al iniciar el firmware. Sin CLI ni consola interactiva.

**Orden de inicialización:**
1. Silencia logs si `BATTERY_MODE=1`
2. Inicializa NVS, event loop, netif y eventfd
3. Inicializa gestión de energía (DFS + light sleep)
4. Inicializa sensor simulado
5. Inicia OpenThread (radio 802.15.4)
6. Unión automática a red Thread (headless)
7. Verifica registro (el nodo expone `/sys/info`)
8. Inicia servidor CoAP en tarea separada
9. Loop principal: leer sensor cada 30s, evaluar umbrales, notificar

#### `node_config.h` — Configuración del usuario

Único archivo que se modifica al agregar un nuevo nodo a la malla. Contiene:

| Sección | Parámetros |
|---|---|
| **Identidad** | `NODE_ID`, `ZONE_ID`, `NODE_TYPE`, `POS_X`, `POS_Y`, `FW_VERSION` |
| **Red Thread** | `THREAD_CHANNEL`, `THREAD_PANID`, `THREAD_NETWORK_NAME`, `THREAD_NETWORK_KEY`, `THREAD_EXT_PAN_ID`, `THREAD_PSKC` |
| **Sensor** | `TEMP_BASELINE`, `HUM_BASELINE`, `SAMPLE_INTERVAL_MS` |
| **Telemetría** | `HEARTBEAT_INTERVAL_S`, `TEMP_THRESHOLD_C`, `HUM_THRESHOLD_PCT` |
| **Energía** | `POWER_SAVE_ENABLED`, `BATTERY_MODE` |
| **CoAP** | `COAP_PORT` |

#### `thread_auto_join.c` — Unión headless

Gestiona la unión a la red Thread sin intervención del usuario.

**Algoritmo:**
1. Obtiene la instancia de OpenThread
2. Intenta leer un dataset activo desde NVS (sesión previa)
3. Si no existe, crea uno nuevo desde `node_config.h` usando `otDatasetSetActive()`
4. Llama a `esp_openthread_auto_start()` con el dataset
5. Espera hasta 60s a que el rol del dispositivo sea ROUTER o CHILD
6. Si no logra unirse, retorna `ESP_FAIL` pero el sistema continúa

**Persistencia:** El dataset se guarda automáticamente en NVS. En reinicios posteriores, se reutiliza sin necesidad de reconfigurar.

#### `sensor_sim.c` — Sensor simulado

Genera lecturas realistas de temperatura y humedad para pruebas sin hardware.

- **Temperatura:** `TEMP_BASELINE + seno(periodo 20 min) + ruido ±0.1°C`
- **Humedad:** `HUM_BASELINE - seno(periodo 20 min) + ruido ±1%`

Para reemplazar con un sensor real (DHT22, BME280, SHT30, etc.), solo se modifica la implementación de `sensor_leer()` — la interfaz no cambia.

#### `coap_handlers.c` — Servidor CoAP

Implementa el servidor CoAP con 4 recursos, manejo de Observers y notificaciones.

**Tres tareas separadas:**
- `app_main` loop (lectura de sensor, evaluación de umbrales)
- `coap_server` task (servidor CoAP, escucha peticiones)
- OpenThread task (mantiene la red)

**Mecanismo de notificación:**
- El gateway se subscribe via `GET /env/temp` con `Observe: 0`
- El nodo notifica automáticamente cuando se cumple un umbral o heartbeat
- Las notificaciones son NON (no requieren ACK)
- El gateway nunca necesita hacer polling

#### `registration.c` — Exposición de identidad

El nodo NO envía activamente su registro. En su lugar, expone `/sys/info` como recurso GET. El gateway es responsable de:
1. Detectar nuevos dispositivos en la red Thread (via OTBR)
2. Consultar `GET coap://[ipv6]/sys/info`
3. Almacenar `id, zone, type, x, y, ver` en su base de datos
4. Subscribirse a `/env/temp` y `/env/hum` con Observe

#### `power_mgmt.c` — Gestión de energía

Inicializa y reporta el estado de las estrategias de ahorro.

No configura el PM en runtime — delega en el framework de ESP-IDF que se auto-configura desde `sdkconfig`. Solo reporta qué está habilitado.

---

## Flujo de ejecución

```
BOOT
│
├─ nvs_flash_init()
├─ esp_event_loop_create_default()
├─ esp_netif_init()
├─ power_mgmt_init()
│   └─ DFS + Light sleep (según sdkconfig)
│
├─ sensor_init()
│
├─ esp_openthread_start()
│   └─ Inicia radio 802.15.4 y stack Thread
│
├─ thread_auto_join()
│   ├─ ¿Dataset en NVS? → sí → usarlo
│   ├─ ¿No? → crear desde node_config.h
│   └─ esp_openthread_auto_start()
│       └─ Esperar hasta 60s por join
│
├─ registration_check()
│   └─ El nodo expone /sys/info (gateway consulta)
│
├─ coap_server_start()
│   └─ Tarea separada: escucha UDP/5683
│
└─ MAIN LOOP (cada 30s)
    ├─ sensor_leer()
    ├─ coap_check_and_notify()
    │   ├─ ¿Δtemp > 0.5°C? → notificar /env/temp (NON)
    │   ├─ ¿Δhum > 3%? → notificar /env/hum (NON)
    │   └─ ¿45s sin notificar? → heartbeat (NON)
    └─ vTaskDelay(30000ms)
```

---

## Unión a red Thread (headless)

El nodo se une a la red Thread automáticamente al encender, sin necesidad de consola ni comandos.

### ¿Cómo determina a qué red unirse?

1. **Primer arranque:** No hay dataset en NVS. El código llama a `configurar_dataset()` que crea un `otOperationalDataset` con los valores de `node_config.h`:
   - `THREAD_CHANNEL` (canal 802.15.4)
   - `THREAD_PANID` (identificador de red)
   - `THREAD_NETWORK_NAME`
   - `THREAD_NETWORK_KEY` (clave maestra de 16 bytes)
   - `THREAD_EXT_PAN_ID` (8 bytes)
   - `THREAD_PSKC` (pre-shared commissioner key)

2. **Reinicios posteriores:** El dataset queda persistido en NVS por OpenThread. Se reutiliza automáticamente.

3. **Timeout:** Si no se une en 60s (no hay gateway, red incorrecta, etc.), el código continúa igual e intenta en background. El servidor CoAP se inicia de todas formas.

### ¿Qué pasa si la red cambia?

Si se cambian los defines en `node_config.h`, hay que borrar NVS para que se use la nueva configuración. Esto se hace con:

```bash
# Opción 1: desde esptool
python -m esptool --chip esp32c6 -p /dev/ttyACM0 erase_region 0x9000 0x6000

# Opción 2: desde el monitor, si hubiera consola (no disponible aquí)
```

---

## Servidor CoAP

El nodo es un **servidor CoAP** (no cliente). Escucha en UDP/5683 y responde a peticiones del gateway. Nunca inicia conexiones salientes.

### Recursos expuestos

| Recurso | Método | Tipo de respuesta | Payload | Descripción |
|---|---|---|---|---|
| `/env/temp` | GET + Observe | NON | `{t: float16}` (6 B) | Temperatura en °C |
| `/env/hum` | GET + Observe | NON | `{h: uint8}` (4 B) | Humedad en %RH |
| `/sys/info` | GET | CON | `{id, zone, type, x, y, ver}` | Identidad del nodo |
| `/sys/health` | GET | CON | `{batt, rssi, up}` | Estado del nodo |

### Estrategia CON vs NON

| Tipo | Significado | Uso |
|---|---|---|
| **CON** (Confirmable) | Requiere ACK del receptor | Comandos y respuestas importantes (`/sys/info`, `/sys/health`) |
| **NON** (Non-confirmable) | Se envía sin esperar ACK | Telemetría periódica (`/env/temp`, `/env/hum` notifications) |

**Regla:** CON para control/consulta, NON para telemetría. Una notificación de temperatura perdida no es crítica porque la siguiente llegará segundos después.

### Formatos CBOR

#### `/env/temp` — Temperatura

```
CBOR: A1 61 74 F9 hh ll
      │  │  │   │   └─ float16 (2 bytes)
      │  │  │   └─ 0xF9 = half-precision tag
      │  │  └─ "t" (text key)
      │  └─ map(1)
      └─ { key: value }
```

CDDL:
```
env-temp-reading = { t: float16 }
```

Bytes totales: **6** (vs ~40 en JSON)

#### `/env/hum` — Humedad

```
CBOR: A1 61 68 value
      │  │  │   └─ uint8 (1 byte)
      │  │  └─ "h" (text key)
      │  └─ map(1)
      └─ { key: value }
```

CDDL:
```
env-hum-reading = { h: uint8 }
```

Bytes totales: **4**

#### `/sys/info` — Identidad

```json
{
  "id": "NODO-TH-01",
  "zone": "ZONA-A",
  "type": "router_th",
  "x": 25.0,
  "y": 40.0,
  "ver": "1.0.0"
}
```

Formato JSON plano por simplicidad inicial. El gateway lo parsea como texto.

#### `/sys/health` — Salud

```json
{
  "batt": 3100,
  "rssi": -65,
  "up": 3600
}
```

- `batt`: voltaje de batería en mV (simulado)
- `rssi`: intensidad de señal en dBm (simulado)
- `up`: uptime en segundos

---

## Telemetría

### Umbral + Heartbeat

El nodo usa una estrategia combinada para minimizar el tráfico sin perder visibilidad:

#### **Umbral (threshold)**
```
Si |temp - last_notified| > 0.5°C → notificar /env/temp
Si |hum - last_notified| > 3%    → notificar /env/hum
```

#### **Heartbeat**
```
Si pasaron 45s sin ninguna notificación → forzar notificación de ambos
```

Esto garantiza que:
- En estado estable: 1 paquete cada 45s (~80 paquetes/hora)
- En cambios bruscos: notificación inmediata
- El gateway sabe que el nodo está vivo aunque ningún sensor cambie

### Escalabilidad del formato

El payload CBOR usa un map con claves texto. Esto permite agregar nuevos campos en el futuro sin romper compatibilidad:

```c
// Versión actual
{ "t": float16, "h": uint8 }

// Versión futura (gateway ignora campos nuevos)
{ "t": float16, "h": uint8, "luz": uint16, "presion": float16 }
```

El gateway (receptor) simplemente ignora las claves que no entiende.

---

## Registro de nodos

El nodo **no se registra activamente** con el gateway. En su lugar:

1. El nodo se une a la red Thread
2. Expone `/sys/info` como recurso CoAP GET
3. **El gateway es responsable** de descubrir el nodo:
   - Detecta nuevos dispositivos Thread via OTBR
   - Consulta `GET coap://[ipv6-link-local]/sys/info`
   - Almacena la identidad en su base de datos
   - Se subscribe a `/env/temp` y `/env/hum` con Observe

El nodo no mantiene estado de registro — siempre sirve datos a quien los pida.

---

## Gestión de energía

El ESP32-C6 es FTD (Full Thread Device), lo que significa que debe permanecer encendido para reenrutar paquetes de otros nodos. No puede hacer deep sleep. Las estrategias aplicables son:

| Estrategia | Ahorro | Cómo se activa |
|---|---|---|
| **DFS** (Dynamic Frequency Scaling) | ~15-25% | `CONFIG_PM_DFS_INIT_AUTO=y` — CPU baja a 40 MHz en idle |
| **Light sleep** (CPU entre tramas) | ~60-80% vs activo | `CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP=y` — automático al desconectar USB |
| **WiFi/BT apagados** | ~10 mA | `CONFIG_ESP_WIFI_ENABLED=n`, `CONFIG_BT_ENABLED=n` |
| **Logs silenciados** | ~1-3 mA | `BATTERY_MODE=1` en `node_config.h` |

**Light sleep solo funciona sin USB**: Cuando el ESP32-C6 está conectado por USB, el periférico USB Serial/JTAG impide la entrada a light sleep. En batería, funciona automáticamente.

### Modo batería

El flag `BATTERY_MODE` en `node_config.h` controla el nivel de logs:

```c
#define BATTERY_MODE  1   // Producción: solo WARN y ERROR
#define BATTERY_MODE  0   // Debug: todos los logs visibles
```

En modo batería, `esp_log_level_set("*", ESP_LOG_WARN)` silencia todos los `ESP_LOGI()` del sistema, incluyendo los internos del framework. La UART no transmite datos, ahorrando la corriente de mantenimiento del driver serie.

### Consumo estimado

| Modo | Consumo | Batería 2000mAh |
|---|---|---|
| FTD activo (160 MHz, logs, USB conectado) | ~45 mA | ~44 h |
| FTD + DFS (80-160 MHz, sin logs) | ~15 mA | ~5.5 días |
| FTD + DFS + light sleep (sin USB) | ~2 mA promedio | ~41 días |
| FTD + DFS + light sleep + tráfico bajo | ~1 mA promedio | ~83 días |

Los valores son estimaciones basadas en documentación de Espressif y mediciones de la comunidad. El consumo real depende del tráfico de red, cantidad de nodos en la malla e intervalo de muestreo.

---

## Comunicación gateway ↔ nodo

### Modelo de comunicación

```
Gateway (CoAP Client)               Nodo (CoAP Server)
       │                                    │
       │ 1. Descubre nodo via Thread        │
       │                                    │
       │ 2. GET /sys/info ────CON─────────► │  (una vez)
       │◄───────── 2.05 Content ──ACK────── │
       │                                    │
       │ 3. GET /env/temp ──Observe:0─────► │  (una vez)
       │◄──── 2.05 Content + Observe(NON)── │  (valor actual)
       │                                    │
       │ 4. (tiempo pasa, temp sube)        │
       │◄──── 2.05 Content + Observe(NON)── │  (cambio > umbral)
       │                                    │
       │ 5. (45s sin cambios)               │
       │◄──── 2.05 Content + Observe(NON)── │  (heartbeat)
       │                                    │
       │ 6. GET /sys/health ────CON────────►│  (bajo demanda)
       │◄───────── 2.05 Content ──ACK────── │
```

### Flujo de datos completo

```
1. Gateway detecta nuevo nodo en Thread
   └─ GET /sys/info → obtiene id, zona, posición

2. Gateway subscribe a temperatura
   └─ GET /env/temp + Observe:0
   └─ A partir de ahora recibe notificaciones automáticas

3. Gateway subscribe a humedad
   └─ GET /env/hum + Observe:0
   └─ Recibe notificaciones automáticas

4. Gateway verifica salud periódicamente
   └─ GET /sys/health (bajo demanda, ej: cada 5 min)

5. Gateway envía datos al dashboard/ThingsBoard
   └─ Convierte CoAP → MQTT/HTTP según sea necesario
```

---

## Cómo agregar un nuevo nodo

Para añadir un nodo T/H a la malla:

1. **Copiar el proyecto**

```bash
cp -r Nodo-router-t-h Nodo-router-t-h-02
```

2. **Editar `main/node_config.h`**

```c
#define NODE_ID             "NODO-TH-02"
#define ZONE_ID             "ZONA-B"
#define POS_X               55.0f
#define POS_Y               30.0f
#define FW_VERSION          "1.0.0"
```

Los parámetros de red Thread (PAN ID, canal, clave) deben ser idénticos en todos los nodos de la misma malla.

3. **Compilar y flashear**

```bash
cd Nodo-router-t-h-02
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

4. **El nodo se une automáticamente**
   - Sin consola, sin comandos
   - Enciende → busca red Thread → se une → expone CoAP
   - El gateway lo descubre y subscribe

---

## Dependencias

| Componente | Versión | Fuente |
|---|---|---|
| ESP-IDF | ≥5.5.0 | Espressif |
| `espressif/coap` | ^4.3.0 | IDF Component Manager |
| `espressif/esp_ot_cli_extension` | ~2.0.0 | IDF Component Manager |
| `ot_examples_common` | local | IDF examples |
| `ot_led` | local | IDF examples (opcional) |

### Instalación

```bash
# Clonar ESP-IDF (si no está instalado)
git clone -b v5.5.4 https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c6

# Configurar entorno
source export.sh

# Compilar
cd Nodo-router-t-h
idf.py build

# Flashear
idf.py -p /dev/ttyACM0 flash

# Monitorear (modo debug, con BATTERY_MODE=0)
idf.py -p /dev/ttyACM0 monitor
```
