# Nodo Sensor SED — ESP32-C6 Thread Ultra Bajo Consumo v5.0

Firmware para **ESP32-C6 como Sleepy End Device (SED)**: nodo Thread con servidor CoAP
que expone datos unificados de telemetria via Observe. Sin POST, sin encoder dinamico,
sin hardcodeo de destinos. ~400 lineas de C, arquitectura limpia.

## Arquitectura

```
BOOT
├── nvs_flash_init()
├── config_init()        ← carga umbrales/coords/intervalo desde NVS
├── esp_openthread_start() → SED (MTD, poll 5s, tickless idle, 80 MHz)
├── join_thread_network()
├── coap_server_start()  ← bind MLEID:5683, registra /env (Observe) + /config (PUT)
├── registration_send_once() ← CON POST /register → Bridge:5685 (una vez)
│
└── while(1):
      lectura = sensor_leer()
      coap_server_notify(t, h)   ← notifica observadores si umbral/HB
      vTaskDelay(5000)           ← deep sleep 5s (tickless idle)
```

## Recursos CoAP

| Recurso | Metodo | Payload | Descripcion |
|---------|--------|---------|-------------|
| `/env` | GET + Observe | `{t, h, b, r, u}` CBOR (~25 B) | Telemetria unificada |
| `/sys/info` | GET | JSON | Identidad: id, zona, tipo, lat, lng, version |
| `/config` | PUT | JSON `{tt,ht,hb,si,lat,lng}` | Configuracion persistente en NVS |
| `/sys/reboot` | PUT | (sin payload) | Reinicio remoto |

## CBOR /env (5 keys, ~25 bytes)

```
A5                          # map(5)
  61 74  F9 hh ll           # "t": float16 temp (5 B)
  61 68  18 hh              # "h": uint8 humedad (4 B)
  61 62  19 hh ll           # "b": uint16 bateria (5 B)
  61 72  38 hh              # "r": negint RSSI (4 B)
  61 75  1A hh hh hh hh     # "u": uint32 uptime (7 B)
```

## Registro CON (una vez por arranque)

El nodo envia CON POST `/register` al Bridge con su identidad completa.
Solo una vez por arranque o reinicio. Payload CBOR (~60 bytes):

```
A6                          # map(6)
  62 69 64  text(NODE_ID)   # "id"
  62 74 70  text(NODE_TYPE) # "tp"
  61 76     text(FW_VERSION) # "v"
  62 7A 6E  text(ZONE_ID)   # "zn"
  63 6C 61 74  FA hh hh hh hh  # "lat": float32
  63 6C 6E 67  FA hh hh hh hh  # "lng": float32
```

Reintenta hasta 5 veces con backoff de 3s. Si falla, sigue operando sin registro.

## Configuracion persistente en NVS

`PUT /config {"tt":1.0,"ht":5,"hb":60,"si":10000,"lat":5.03,"lng":-75.47}`

| Llave | Significado | Rango | Default |
|-------|------------|-------|---------|
| `tt` | Umbral temp (°C) | 0.1 -- 10.0 | 0.5 |
| `ht` | Umbral hum (%) | 1 -- 50 | 3 |
| `hb` | Heartbeat (s) | 10 -- 600 | 45 |
| `si` | Sample interval (ms) | 1000 -- 60000 | 5000 |
| `lat` | Latitud GPS | -90 -- 90 | 5.029139 |
| `lng` | Longitud GPS | -180 -- 180 | -75.472724 |

Todos los cambios persisten en NVS y sobreviven reinicios.
Al arrancar, `config_init()` carga NVS; si esta vacio, usa los defaults de `node_config.h`.

## Bajo consumo

| Estrategia | Impacto |
|-----------|---------|
| MTD/SED | Radio 802.15.4 duerme entre polls (~0 mA idle) |
| Poll period 5s | Despierta 200ms cada 5s para preguntar al padre |
| CPU 80 MHz | ~15 mA activo vs ~22 mA a 160 MHz |
| Tickless idle | CPU en light sleep durante vTaskDelay (~10 uA) |
| Observe | Sin POSTs redundantes; el cliente recibe solo cuando hay cambio |
| Payload CBOR fijo | ~25 B vs ~150 B JSON, encoder estatico sin malloc |
| Corriente promedio | **~15 uA** (~12 años con 2000 mAh en modo campo) |

## Estructura de archivos

```
main/
├── nodo_th_auto.c          # Punto de entrada + main loop
├── coap_server.c/h          # Servidor CoAP: /env (Observe), /config (PUT)
├── registration_client.c/h  # CON POST /register una sola vez
├── config.c/h               # NVS persistente (umbrales, coords, intervalo)
├── sensor_sim.c/h           # Simulador de sensores
├── node_config.h            # Identidad, defaults, red
├── esp_ot_config.h          # Radio OpenThread nativa
├── esp_ot_custom_config.h   # Observe + pollperiod
├── idf_component.yml        # libcoap ^4.3.0
└── CMakeLists.txt
```

## Compilacion y flash

```bash
cd ~/Documentos/Semestre_VII/Granja-IOT/Nodo-sensor-SED
. ~/.espressif/v5.5.4/esp-idf/export.sh

# SED (bajo consumo):
rm -f sdkconfig
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.sed" idf.py reconfigure
idf.py build
idf.py -p /dev/ttyACM0 flash

# FTD (desarrollo, sin sleep, mas facil depurar):
rm -f sdkconfig
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Red Thread

| Parametro | Valor |
|-----------|-------|
| Network Name | `OpenThread-5eac` |
| Channel | 17 |
| PAN ID | `0x5eac` |
| Mesh-Local Prefix | `fd29:c51e:a87a:e5e5::/64` |
| Bridge | `[fd29:c51e:a87a:e5e5:0:ff:fe00:fc00]:5685` |

## Logs esperados

```
I (394) nodo: === Nodo SED [NODO-SENSOR-1] v5.0.0 ===
I (404) nodo: Zona: ZONA-A | Bridge: [fd29:...:fc00]:5685
I (414) cfg: NVS vacio, usando defaults
I (524) cfg: Config: T>0.5C H>3% HB=45s SI=5000ms Lat=5.029139 Lng=-75.472724
I (1024) nodo: Dataset configurado
I (1124) nodo: Interfaz IPv6 up
I (1224) nodo: Thread iniciado, uniendo a red...
I (5224) srv: CoAP servidor en [fd29:...:e5e5]:5683
I (5224) srv: Recursos: /env (GET+Observe) /sys/info /config (PUT) /sys/reboot
I (6224) nodo: Enviando registro al Bridge...
I (6324) reg: Registro ACK 2.xx
I (6324) reg: Registro exitoso en intento 1
I (6324) nodo: Loop: leer sensor cada 5000ms → notificar → deep sleep
I (11224) srv: notify temp: 25.06 C (delta=0.56)
I (16224) srv: notify hum: 58 % (delta=4)
I (51224) srv: heartbeat: T=25.84 H=57
```
