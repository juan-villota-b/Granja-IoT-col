# Nodo CoAP SED Válvula — ESP32-C6 Thread + Actuador de Riego

Firmware para **ESP32-C6 como Sleepy End Device (SED)**: nodo Thread con control de válvula vía CoAP, derivado del nodo sensor TH. Incluye downlink piggyback para abrir/cerrar válvula solenoide y mismos modos de bajo consumo.

> Derivado de `nodo-coap-sed/`. Este nodo incluye control de actuador: recibe comandos `set_valve` desde el dashboard vía la respuesta CoAP del bridge y acciona un relé.

## Diferencia con nodo-coap-sed

| Carpeta | Nodo físico | ID | Tipo | Coordenadas |
|---------|-------------|----|------|-------------|
| `nodo-coap-sed/` | NODO-TH-AUTO-1 | `NODO-TH-AUTO` | `th_auto` | 5.031303, -75.468862 |
| **`nodo-coap-sed-valve/`** | **NODO-VALVE-AUTO** | **`NODO-VALVE-AUTO`** | **`valve`** | **5.031500, -75.469100** |

Ambos firmware son estructuralmente idénticos. La configuración específica (ID, zona, coordenadas, tipo) se define en `main/node_config.h`.

## Arquitectura del firmware

```
app_main()
  │
  ├── nvs_flash_init()
  ├── esp_event_loop_create()
  ├── esp_netif_init()
  ├── sensor_init()
  ├── config_init()
  ├── esp_openthread_start()     // Arranca stack OpenThread (MTD/SED)
  ├── join_thread_network()       // Carga dataset, ifconfig up, thread start
  │
  └── while(1) {
        lectura = sensor_leer()
        if coap_should_send(t, h) {    // ¿cambió temp>0.5°C o hum>3% o HB?
            coap_send_telemetry(t, h)  // POST CON → Bridge :5685
            └── _encode_telemetry()    // CBOR: 11 o 6 claves
            └── POST /readings
            └── espera ACK (5s timeout)
            └── _parse_downlink()      // comando? abrir/cerrar válvula
        }
        vTaskDelay(30000)              // + tickless idle = light sleep
      }
```

## Estructura de archivos

```
nodo-coap-sed/
├── main/
│   ├── nodo_th_auto.c        # Punto de entrada: init + SED loop
│   ├── coap_client.c          # Cliente CoAP: POST + CBOR + umbrales
│   ├── coap_client.h          # coap_send_telemetry(), coap_should_send()
│   ├── coap_handlers.c        # Servidor CoAP: /env/temp, /env/hum, /sys/info, /sys/health
│   ├── sensor_sim.c/h         # Simulador de sensores
│   ├── config.c/h             # Configuración NVS
│   ├── node_config.h          # LAT/LNG, BRIDGE_IPV6, puerto, umbrales
│   ├── registration.c/h       # Registro de nodo (placeholder)
│   ├── esp_ot_config.h        # Config radio OpenThread nativa
│   ├── esp_ot_custom_config.h # CoAP Observe API
│   ├── idf_component.yml      # coap ^4.3.0
│   └── CMakeLists.txt
├── sdkconfig.defaults         # Base FTD (para referencia)
├── sdkconfig.sed              # Overrides SED: MTD + 80 MHz + tickless idle
└── partitions.csv
```

## Estrategias de bajo consumo

### 1. Filtro de umbrales — `coap_should_send()`

```c
bool coap_should_send(float temp, uint8_t hum)
{
    // Solo envía si temp cambió > 0.5°C, hum > 3%, o heartbeat 300s
    bool thresh = (fabsf(temp - last_temp) > TEMP_THRESHOLD_C) ||
                  (abs(hum - last_hum) > HUM_THRESHOLD_PCT);
    bool hb     = (now - last_send) >= HEARTBEAT_INTERVAL_S * 1000;
    return thresh || hb;
}
```

**Ahorro:** en campo (temp cambia ~0.5°C cada 10 min) pasa de 120 POSTs/hora a ~6-12/hora.

### 2. Primer POST con atributos — `_g_registered`

Solo el primer POST exitoso envía 11 claves (77 B). Los siguientes solo 6 (39 B). La bandera `_g_registered` solo se activa tras recibir ACK del bridge.

**Ahorro:** ~50% menos bytes transmitidos después del registro.

### 3. CPU a 80 MHz (sdkconfig.sed)

```kconfig
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_80=y
# CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160 is not set
```

Reduce corriente activa de ~22 mA a ~15 mA. El tiempo de procesamiento solo aumenta ~50% pero sigue siendo insignificante (<200ms por ciclo).

### 4. Thread MTD/SED (sdkconfig.sed)

```kconfig
CONFIG_OPENTHREAD_FTD=n
CONFIG_OPENTHREAD_MTD=y
```

El radio 802.15.4 duerme entre transmisiones. El nodo no rutea paquetes para otros. El stack OpenThread maneja el sleep automáticamente.

**Ahorro:** ~20 mA (FTD, radio siempre ON) → ~0 mA (SED, radio OFF entre TX).

### 5. Tickless idle (sdkconfig.sed)

```kconfig
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y
CONFIG_FREERTOS_TICKLESS_IDLE_DYNAMIC=y
```

Cuando el procesador no tiene tareas que ejecutar (durante `vTaskDelay(30000)`), FreeRTOS entra en light sleep automático. El CPU se apaga hasta el próximo tick.

**Ahorro:** ~15 mA → ~10 µA durante el delay de 30s. Duty cycle del CPU: <0.5%.

### 6. Payload CBOR compacto (6-11 claves, 39-77 bytes)

En vez de enviar JSON (~150 bytes), se usa CBOR binario con nombres de clave de 1-2 caracteres y tipos compactos (half-float, uint8, uint16, negint).

**Ahorro:** ~60-75% menos bytes por POST.

### 7. COAP_TIMEOUT_MS = 5000 (node_config.h)

Timeout de 5s para esperar ACK. Si el bridge no responde en 5s, aborta y reintenta en el siguiente ciclo. Sin necesidad de retransmisiones infinitas.

### Tabla de ahorro acumulado

| Estrategia | Sin optimizar | Optimizado |
|-----------|--------------|------------|
| Filtro de umbrales | 120 POSTs/h (cada 30s) | ~6 POSTs/h (campo) |
| Payload CBOR | ~150 B (JSON) | ~39 B (CBOR) |
| CPU | 160 MHz (22 mA) | 80 MHz (15 mA) |
| Thread | FTD (20 mA) | SED (~0 mA idle) |
| Tickless idle | CPU siempre ON | Light sleep (10 µA) |
| **Total promedio** | **~20-30 mA** | **~15 µA** |

## Consumo estimado — batería 2000mAh

| Modo | Corriente promedio | Duración |
|------|-------------------|----------|
| FTD + POST 30s (original) | ~20 mA | ~4 días |
| SED + POST 30s (sin filtro) | ~92 µA | ~2 años |
| SED + umbrales + 80 MHz | ~15 µA | **~12 años** |

## Formato CBOR de telemetría

### Primer POST (registro — 11 claves, ~81 bytes)

```cbor
AB                          # map(11)
  62 69 64  6D ...          # "id": "NODO-TH-AUTO"
  61 74     F9 HH LL        # "t": half-float
  61 68     18 HH           # "h": uint8
  61 62     19 HH LL        # "b": uint16
  61 72     38 40           # "r": -65 dBm
  61 75     1A HH HH HH HH  # "u": uint32
  62 7A 6E  66 ...          # "zn": "ZONA-A"
  62 74 70  67 ...          # "tp": "th_auto"
  63 6C 61 74  FA HH HH HH HH  # "lat": float32 (precisión completa)
  63 6C 6E 67  FA HH HH HH HH  # "lng": float32 (precisión completa)
  61 76     65 312E...      # "v": "1.0.0"
```

> **Nota:** `lat`/`lng` se codifican como **CBOR float32** (0xFA, 4 bytes)
> en vez de half-float (0xF9, 2 bytes) para preservar la precisión
> de coordenadas GPS (~1m). Las keys `x`/`y` fueron reemplazadas por
> `lat`/`lng` en la versión 2026-06-03.

### POSTs siguientes (solo telemetría — 6 claves, ~39 bytes)

`lat`/`lng`, `zn`, `tp` y `v` solo se envían en el primer POST (registro).
Los siguientes solo llevan datos de telemetría:

```cbor
A6                          # map(6)
  62 69 64  6D ...          # "id"
  61 74     F9 HH LL        # "t"
  61 68     18 HH           # "h"
  61 62     19 HH LL        # "b"
  61 72     38 40           # "r"
  61 75     1A HH HH HH HH  # "u"
```

## Downlink: comandos desde TB Edge (piggyback)

```
TB Edge ──RPC MQTT──→ Bridge ──encola──→ pending_commands[nid]
Nodo ──POST /readings──→ Bridge ──ACK con comando──→ Nodo ejecuta
```

| Comando | Payload | Acción |
|---------|---------|--------|
| `set_valve` | `{"v": 1}` | Abre válvula |
| `set_valve` | `{"v": 0}` | Cierra válvula |
| `set_thresholds` | `{"tt":2.0,"ht":5,"hb":60}` | Cambia umbrales |
| (sin comando) | `{}` | Nada |

## Configuración (node_config.h)

```c
#define NODE_ID             "NODO-TH-AUTO"
#define ZONE_ID             "ZONA-A"
#define LAT                 5.031303f   // Coordenada GPS (reemplaza POS_X)
#define LNG                 -75.468862f // Coordenada GPS (reemplaza POS_Y)
#define FW_VERSION          "1.0.0"
#define BRIDGE_IPV6         "fd29:c51e:a87a:e5e5:0:ff:fe00:fc00"
#define COAP_SERVER_PORT    5685
#define COAP_TIMEOUT_MS     5000
#define SAMPLE_INTERVAL_MS  30000
#define TEMP_THRESHOLD_C    0.5f
#define HUM_THRESHOLD_PCT   3
#define HEARTBEAT_INTERVAL_S  300
```

> `LAT`/`LNG` se envían al bridge como CBOR float32 durante el registro
> y se almacenan como atributos del dispositivo en ThingsBoard.
> El dashboard los usa para posicionar el nodo en OpenStreetMap.

## Red Thread

| Parámetro | Valor |
|-----------|-------|
| Network Name | `OpenThread-5eac` |
| Channel | 17 |
| PAN ID | `0x5eac` |
| Extended PAN ID | `6eb779829999c73a` |
| Mesh-Local Prefix | `fd29:c51e:a87a:e5e5::/64` |
| Master Key | `160fe4f6d201115a746d0802332f6e77` |

## Compilación y flash (SED)

```bash
cd ~/Documentos/Semestre_VII/Granja-IOT/nodo-coap-sed
. ~/.espressif/v5.5.4/esp-idf/export.sh

# 1ra vez: regenerar sdkconfig con perfil SED
rm -f sdkconfig
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.sed" idf.py reconfigure

# Compilar
idf.py build

# Flashear
idf.py -p /dev/ttyACM1 flash
```

## Logs esperados (serial)

```
I (394) nodo: === Nodo SED [NODO-TH-AUTO] ===
I (404) nodo: Zona: ZONA-A | Bridge: fd29:...:5685 | v1.0.0 | HB=300s | T>0.5°C H>3%
I (524) nodo: Loop: leer sensor -> umbral? -> POST /readings -> sleep
I (544) coap_client: POST /readings -> fd29:...:5685 (81 B + attrs) t=25.06 h=60
                    lat=5.031303 lng=-75.468862
I (944) coap_client: REGISTRO OK -- BRIDGE CONFIRMÓ ATRIBUTOS
I (3164) coap_client: POST /readings -> fd29:...:5685 (39 B) t=25.36 h=58
I (33164) coap_client: POST /readings -> fd29:...:5685 (39 B) t=25.55 h=56
```

## API interna

### coap_client.h
```c
esp_err_t coap_send_telemetry(float temp_c, uint8_t hum_pct);
bool      coap_should_send(float temp_c, uint8_t hum_pct);
```

### sensor_sim.h / config.h
```c
typedef struct { float temperatura_c; uint8_t humedad_pct; } sensor_lectura_t;
void sensor_init(void); sensor_lectura_t sensor_leer(void);
void config_init(void); void config_save(void);
```

---

## Registro de cambios

### 2026-06-03 — Coordenadas GPS reales
- `node_config.h`: `POS_X`/`POS_Y` reemplazados por `LAT`/`LNG` (float32)
- `coap_client.c`: CBOR keys `x`/`y` → `lat`/`lng`. Codificadas como **float32** (0xFA, 4 bytes) en vez de half-float (0xF9) para preservar precisión GPS (~1m)
- `coap_handlers.c`: `/sys/info` retorna `lat`/`lng` en vez de `x`/`y`
- Bridge: mapea `lat`/`lng` → atributos TB (`lat`, `lng`)
- Dashboard: lee `lat`/`lng` directamente del dispositivo y lo posiciona en OpenStreetMap
- Coordenadas actuales: `LAT=5.031303, LNG=-75.468862` (Manizales, Colombia)
