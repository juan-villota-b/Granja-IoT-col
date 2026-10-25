# soil-node — Nodo-Humedad

Nodo sensor de humedad de suelo sobre ESP32-C6 en modo **Sleepy End Device** de Thread. Lee un HW-390 por ADC y publica el porcentaje de humedad.

| Dato | Valor |
|------|-------|
| `NODE_ID` | `Nodo-Humedad` |
| Zona | `ZONA-C` |
| Variable publicada | `humidity` (%) |
| Sensor | HW-390 en ADC1_CH4 (12 bits, calibración por curva) |
| Umbral de envío | 5 % de cambio |
| Heartbeat | 300 s |
| Muestreo | 30 s |

## Módulos

| Archivo | Responsabilidad |
|---------|-----------------|
| `main/main.c` | Arranque, join Thread, lazo de muestreo y decisión de envío |
| `main/sensor_soil.c` | Lectura del ADC, conversión a mV e interpolación de humedad |
| `main/coap_client.c` | Registro y telemetría CoAP/CBOR; decodificación de strings CBOR |
| `main/runtime_config.c` | Umbral, heartbeat e intervalo persistidos en NVS |
| `main/node_config.h` | Identidad, posición y destino CoAP |

## Curva de calibración

El HW-390 entrega **más** voltaje en seco y **menos** en mojado:

| Condición | Voltaje | Humedad |
|-----------|---------|---------|
| Aire / suelo seco | ≥ 2800 mV | 0 % |
| Suelo húmedo / agua | ≤ 1300 mV | 100 % |

Entre ambos extremos la humedad se interpola linealmente. Los valores están en `sensor_soil.h` (`HW390_AIR_MV`, `HW390_WATER_MV`) y conviene recalibrarlos para el suelo de la finca.

## Conexión

| Señal | GPIO |
|-------|------|
| HW-390 AO | GPIO4 · ADC1_CH4 |
| HW-390 VCC | 3V3 |

## Compilar

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```
