# Granja IoT

Granja IoT con red **OpenThread** y gateway **ThingsBoard Edge**.

## Estructura

### `Nodo-router-t-h/` — Nodo sensor Thread (FTD)

Firmware para **ESP32-C6** con ESP-IDF 5.5+. Es un Router Full Thread Device que mide temperatura y humedad, y expone recursos **CoAP** (`/env/temp`, `/env/hum`, `/sys/health`, `/sys/info`). Soporta configuración remota, reinicio OTA, y modos de bajo consumo (DFS + light sleep).

Compilar y flashear:
```bash
cd Nodo-router-t-h
idf.py build flash monitor
```

### `Raspberry-pi-4/` — Gateway IoT

Archivos para la Raspberry Pi 4 (Debian 13), incluye:

- **`iot-gateway/`** — Docker Compose con OTBR, ThingsBoard Edge 4.2.0 y PostgreSQL
- **`bridge/`** — Orquestador Python que descubre nodos CoAP, lee telemetría y la publica a ThingsBoard Edge via MQTT
- **`tb-edge/`** y **`otbr/`** — Versiones standalone para pruebas
