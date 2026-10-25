# light-node — Nodo-Luz

Nodo sensor de luminosidad sobre ESP32-C6 en modo **Sleepy End Device** de Thread. Lee un LDR por ADC y publica el porcentaje de luz al bridge.

| Dato | Valor |
|------|-------|
| `NODE_ID` | `Nodo-Luz` |
| Zona | `ZONA-A` |
| Variable publicada | `light` (%) |
| Sensor | LDR en ADC1_CH4 (12 bits) |
| Umbral de envío | 5 % de cambio |
| Heartbeat | 300 s |
| Muestreo | 30 s |

## Módulos

| Archivo | Responsabilidad |
|---------|-----------------|
| `main/main.c` | Arranque, join Thread, lazo de muestreo y decisión de envío |
| `main/sensor_light.c` | Lectura del ADC y conversión a porcentaje (`raw / 4095 × 100`) |
| `main/coap_client.c` | Registro y telemetría CoAP/CBOR con reintentos |
| `main/runtime_config.c` | Umbral, heartbeat e intervalo persistidos en NVS |
| `main/node_config.h` | Identidad, posición y destino CoAP |

## Conexión

| Señal | GPIO |
|-------|------|
| LDR (divisor resistivo) | GPIO4 · ADC1_CH4 |

## Compilar

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

En el log de arranque se debe leer `SED mode: rx=0 devtype=0 netdata=1`. El payload y el protocolo se describen en [`../../docs/architecture.md`](../../docs/architecture.md).
