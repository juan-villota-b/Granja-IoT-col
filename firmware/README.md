# Firmware — Nodos ESP32-C6

Firmware ESP-IDF de los cuatro nodos de campo. Cada nodo es un **Sleepy End Device** de Thread que muestrea su sensor, decide si el cambio es significativo y envía un `POST` CoAP con payload CBOR al bridge del gateway.

| Nodo | Carpeta | Sensor / actuador | Variable | Periodo |
|------|---------|-------------------|----------|---------|
| Nodo-Luz | [`nodes/light-node`](nodes/light-node) | LDR (ADC1_CH4) | `light` % | 30 s |
| Nodo-Temperatura | [`nodes/temperature-node`](nodes/temperature-node) | DHT22 (GPIO4) | `temperature` °C | 30 s |
| Nodo-Humedad | [`nodes/soil-node`](nodes/soil-node) | HW-390 (ADC) | `humidity` % | 30 s |
| BOMBA | [`nodes/valve-node`](nodes/valve-node) | Relé GPIO7 + pulsador GPIO0 | `valve` 0/1 | 1 s |

---

## Estructura de un nodo

Todos los nodos comparten la misma organización de módulos:

```
nodes/<node>/
├── CMakeLists.txt           # Proyecto ESP-IDF (MINIMAL_BUILD)
├── partitions.csv           # nvs + phy + factory (4 MB)
├── sdkconfig.defaults       # Objetivo esp32c6 + perfil de bajo consumo
├── main/
│   ├── CMakeLists.txt       # SRCS y dependencias del componente
│   ├── main.c               # app_main: arranque, join Thread, lazo de telemetría
│   ├── node_config.h        # Identidad del nodo y constantes de red (editable)
│   ├── runtime_config.c/.h  # Configuración persistente en NVS
│   ├── coap_client.c/.h     # Cliente CoAP: registro, telemetría, reintentos
│   ├── sensor_*.c/.h        # Driver del sensor del nodo
│   ├── esp_ot_config.h      # Configuración OpenThread de ESP-IDF
│   └── esp_ot_custom_config.h # Overrides de OpenThread
└── README.md
```

| Módulo | Responsabilidad |
|--------|-----------------|
| `main.c` | Inicializa NVS, event loop, netif y OpenThread; fuerza el modo SED; ejecuta el lazo de muestreo |
| `node_config.h` | `NODE_ID`, zona, posición, `PROV_KEY`, dirección IPv6 y puerto del bridge |
| `runtime_config.*` | Umbral, heartbeat e intervalo de muestreo, con respaldo en NVS |
| `coap_client.*` | Codificación CBOR, sesión CoAP CON, reintentos y lectura del downlink |
| `sensor_*` | Lectura del hardware y conversión a unidad física |

---

## Compilar y flashear

```bash
. $IDF_PATH/export.sh

cd nodes/light-node
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Antes de compilar, ajustar en `main/node_config.h`:

| Macro | Descripción |
|-------|-------------|
| `NODE_ID` | Nombre del dispositivo en ThingsBoard |
| `ZONE_ID` | Zona de cultivo |
| `PROV_KEY` | Access token del dispositivo en ThingsBoard |
| `LAT_DEFAULT` / `LNG_DEFAULT` | Posición en el mapa |
| `BRIDGE_IPV6` / `BRIDGE_PORT` | Destino CoAP del bridge |

`sdkconfig.defaults` ya fija 80 MHz, `CONFIG_OPENTHREAD_MTD=y`, `CONFIG_PM_ENABLE=y` y *tickless idle*; no se necesita un archivo de defaults adicional.

---

## Lazo de telemetría

```c
debe_enviar = (|valor - valor_anterior| >= umbral)
           || (uptime - ultimo_envio >= heartbeat);
```

- **Primer envío:** registro con `id`, `tp`, `v`, posición, RSSI y uptime.
- **Envíos siguientes:** solo `id`, variable, RSSI y uptime.
- **Reintentos:** 3 en el registro, 2 en telemetría, timeout de 1500 ms por intento.
- **Downlink:** el bridge adjunta el comando en la respuesta; solo `valve-node` lo interpreta.

El modo SED se aplica antes de habilitar Thread y se vuelve a comprobar tras el *join*, porque OpenThread persiste `rx-on-when-idle` en NVS. En el log de arranque debe aparecer:

```
SED mode: rx=0 devtype=0 netdata=1
```

Los detalles de protocolo, payloads y consumo están en [`../docs/architecture.md`](../docs/architecture.md).
