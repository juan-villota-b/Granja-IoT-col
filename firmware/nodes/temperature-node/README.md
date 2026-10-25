# temperature-node — Nodo-Temperatura

Nodo sensor de temperatura sobre ESP32-C6 en modo **Sleepy End Device** de Thread. Lee un DHT22 por GPIO y publica la temperatura en grados Celsius.

| Dato | Valor |
|------|-------|
| `NODE_ID` | `Nodo-Temperatura` |
| Zona | `ZONA-B` |
| Variable publicada | `temperature` (°C) |
| Sensor | DHT11/DHT22 en GPIO4 (protocolo one-wire) |
| Umbral de envío | Configurable en NVS (0 °C por defecto: todo cambio dispara) |
| Heartbeat | 300 s |
| Muestreo | 30 s |

## Módulos

| Archivo | Responsabilidad |
|---------|-----------------|
| `main/main.c` | Arranque, join Thread, lazo de muestreo y decisión de envío |
| `main/sensor_dht11.c` | Protocolo one-wire, verificación de checksum y decodificación |
| `main/calibration.c` | Offset de calibración persistido en NVS |
| `main/sensor_types.h` | Estructura de lectura compartida |
| `main/coap_client.c` | Registro y telemetría CoAP/CBOR con reintentos |
| `main/runtime_config.c` | Umbral, heartbeat e intervalo persistidos en NVS |

## Calibración

En el primer arranque el nodo toma la primera lectura y calcula el offset necesario para llevarla a 20 °C. El offset se guarda en la partición NVS (namespace `cal`) y se aplica a todas las lecturas siguientes. Si la lectura real ya es correcta, basta con borrar la NVS para recalibrar:

```bash
idf.py -p /dev/ttyACM0 erase-flash
```

## Conexión

| Señal | GPIO |
|-------|------|
| DHT22 DATA | GPIO4 (pull-up de 10 kΩ a 3V3) |

## Compilar

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```
