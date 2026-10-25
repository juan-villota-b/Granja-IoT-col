# valve-node — BOMBA

Nodo actuador sobre ESP32-C6. Mantiene un periodo de sondeo corto para recibir comandos con baja latencia, acciona un relé y publica el estado de la válvula. Es el único nodo que **interpreta el downlink**.

| Dato | Valor |
|------|-------|
| `NODE_ID` | `BOMBA` |
| Zona | `ZONA-D` |
| Variable publicada | `valve` (0 = cerrada, 1 = abierta) |
| Actuador | Relé en GPIO7 |
| Entrada local | Pulsador en GPIO0 (pull-up, 0 = presionado) |
| Heartbeat | 1 s |
| Periodo de sondeo | 1 s |

## Módulos

| Archivo | Responsabilidad |
|---------|-----------------|
| `main/main.c` | Arranque, join Thread, lazo de 1 s y aplicación del comando recibido |
| `main/actuator.c` | Control del relé y lectura del pulsador |
| `main/coap_client.c` | Telemetría CoAP/CBOR y parseo de `{"v": 0\|1}` en la respuesta |
| `main/runtime_config.c` | Configuración persistida en NVS |
| `main/node_config.h` | Identidad, posición, destino CoAP y formato de telemetría |

## Downlink

El bridge responde cada POST con el comando pendiente:

```cbor
{ "v": 1 }     # abrir
{ "v": 0 }     # cerrar
{ }            # sin comando
```

`push_handler()` detecta el mapa de una entrada con clave `v` y entrega el valor a `main.c`, que llama a `actuator_set()`.

## Conexión

| Señal | GPIO |
|-------|------|
| Relé (módulo optoacoplado) | GPIO7 |
| Pulsador manual | GPIO0 (pull-up interno) |

## Compilar

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

> El nodo no es un sensor: `NODE_TYPE` es `actuator_pump` y el estado de la válvula viaja en la clave `v`. La lógica de decisión de riego vive en el gateway.
