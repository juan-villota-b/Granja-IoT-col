# Sistema Embebido — OTBR + TB Edge + Bridge

Stack mínimo para un dispositivo IoT embebido (Raspberry Pi, BeagleBone, etc.).

## Servicios

| Servicio | Imagen | Rol |
|----------|--------|-----|
| `otbr` | `openthread/border-router:latest` | Border Router Thread (RCP vía UART) |
| `mytbedge` | `thingsboard/tb-edge:4.2.0EDGE` | Edge local de ThingsBoard |
| `postgres` | `postgres:16` | Base de datos para TB Edge |
| `bridge` | Se construye desde `./bridge` | Traductor CoAP → MQTT |

## Requisitos de hardware

- RCP (Radio Co-Processor) nRF52840 conectado por USB en `/dev/ttyACM0`
- Conexión WiFi (`wlan0`) o ethernet para la red Thread
- Al menos 1GB RAM, 4GB disco

## Configuración antes de desplegar

Editar `docker-compose.yml` y cambiar:

1. `OT_INFRA_IF`: interfaz de red (ej: `eth0`, `wlan0`, `wlo1`)
2. `CLOUD_RPC_HOST`: IP del servidor ThingsBoard CE al que se conecta el Edge
3. `START_SIMULATION`: `"false"` para modo real (con sensores físicos)

## Despliegue

```bash
# Copiar esta carpeta al dispositivo embebido
scp -r embedded-system user@raspberrypi.local:~

# En el dispositivo:
cd ~/embedded-system
sudo docker compose up -d
```

## Puertos

| Puerto | Servicio | Uso |
|--------|----------|-----|
| 8082 | TB Edge | Web UI |
| 1884 | TB Edge | MQTT (bridge) |
| 5684 | TB Edge | CoAP (sensores Thread) |
| 8083 | OTBR | Web UI |
| 8081 | OTBR | REST API |

## Notas

- OTBR requiere `privileged: true` y `network_mode: host` para acceso a dispositivos y red
- El bridge se conecta a TB Edge vía MQTT en `localhost:1884`
- TB Edge sincroniza con ThingsBoard CE vía `CLOUD_RPC_HOST:7070` (RPC)
- Los datos persisten en volúmenes Docker (`tb-edge-data`, `tb-edge-logs`, `tb-edge-postgres-data`)
