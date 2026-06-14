# Raspberry-v2 — Gateway IoT de Campo

Infraestructura local en Raspberry Pi 4 que ejecuta el Border Router Thread, ThingsBoard Edge, PostgreSQL y el Bridge Python con control automatico de riego.

## Arquitectura

```
PC (desarrollo local)                  RPi (finca@192.168.1.114) - Produccion
┌──────────────────────┐             ┌──────────────────────────┐
│  TB Edge :8080       │             │  TB Edge 4.2.0EDGE      │
│  Granja Dashboard    │             │  :8082 (web)             │
│  :3000 (Docker)      │             │  :1883 (MQTT)            │
│                      │             │  PostgreSQL 16           │
│  Tailscale Funnel    │             │                          │
│  (HTTPS publico)     │             │  OTBR (host net)         │
└──────────────────────┘             │  wpan0 ← Thread 802.15.4 │
         │                           │                          │
         │ REST API                  │  Bridge CoAP/MQTT        │
         │                           │  [::]:5685 ← POSTs CBOR  │
         ▼                           │  → TB Edge :1883         │
   Dashboard web                     │  + IrrigationController  │
   (mobile-first)                    └──────────────────────────┘
```

## Servicios (docker-compose)

| Servicio | Puerto | Descripcion |
|----------|--------|-------------|
| OTBR | 8083 (web), 8081 (REST) | OpenThread Border Router — nRF52840 RCP via ttyACM0 |
| TB Edge | 8082 (web), 1883 (MQTT) | ThingsBoard Edge 4.2.0EDGE — recibe telemetria y almacena |
| PostgreSQL | interno (:5432) | Base de datos de TB Edge |
| Bridge | 5685/udp (CoAP) | Servidor CoAP + MQTT gateway + control de riego |

## Flujo de datos

```
ESP32-C6 ──POST CBOR──→ Bridge:5685/udp ──MQTT──→ TB Edge:1883
  {id, t, h, b, r, u}     decode+publish       telemetry topic
                                  │
                                  └──→ IrrigationController
                                        evalua humedad, luz, temp, hora
                                        └── RPC valve → TB Edge :8082
```

## Usar

### Subir todo

```bash
cd ~/Raspberry-v2
./start.sh
```

### Bajar todo

```bash
docker compose down
```

### Logs

```bash
docker logs -f iot-bridge                          # Bridge CoAP + irrigation
docker logs -f otbr                                # OTBR
docker logs -f raspberry-v2-mytbedge-1             # TB Edge
docker logs -f iot-bridge | grep -E "AUTO|irrigation"  # Solo riego
```

## Datos de red

- PC (TB Edge dev): `localhost:8080`
- RPi (TB Edge prod): `192.168.1.114:8082`
- RPi (MQTT): `192.168.1.114:1883`
- RPi SSH: `ssh finca@192.168.1.114` (pass: `12345`)
- Credenciales TB: `tenant@thingsboard.org` / `tenant`
- Dashboard (Funnel): `https://granja-iot.tailaf11de.ts.net`

## Configurar gateway en TB Edge

1. Abrir `http://192.168.1.114:8082` → Devices → + → Add device
2. Name: `IoT-Gateway`, ☑ **Is gateway**
3. Copiar Access token
4. Pegar token en `bridge/config.yaml` → `mqtt.username`
5. Reconstruir: `docker compose up -d --build bridge`

## Control automatico de riego

El `IrrigationController` en `bridge/automation/irrigation.py` se ejecuta dentro del contenedor `iot-bridge`. Evalua la telemetria entrante de todos los nodos y decide cuando abrir/cerrar la valvula:

- **ABRIR** si humedad suelo < 30% (con restricciones de luz, temperatura y ventana horaria)
- **CERRAR** inmediatamente si humedad >= 70%
- Cooldown de 60s entre aperturas

Los comandos RPC se envian directo a TB Edge (`192.168.1.114:8082`), sin pasar por la nube.

### Actualizar umbrales

Editar `bridge/config.yaml` en la RPi y reconstruir el bridge:

```bash
ssh finca@192.168.1.114
nano ~/Raspberry-v2/bridge/config.yaml
docker compose -f ~/Raspberry-v2/docker-compose.yml up -d --build bridge
```

### Despliegue de cambios en el bridge

```bash
# Copiar archivos locales a la RPi
scp Raspberry-v2/bridge/automation/irrigation.py finca@192.168.1.114:~/Raspberry-v2/bridge/automation/
scp Raspberry-v2/bridge/automation/__init__.py finca@192.168.1.114:~/Raspberry-v2/bridge/automation/

# Reconstruir el contenedor
ssh finca@192.168.1.114 'cd ~/Raspberry-v2 && docker compose up -d --build bridge'
```

## Estructura de archivos

```
Raspberry-v2/
├── docker-compose.yml      # Servicios: otbr, mytbedge, postgres, bridge
├── start.sh                # Entrypoint con --build forzado
├── install-service.sh      # Instalar como servicio systemd
├── bridge/                 # Bridge Python (CoAP + MQTT + irrigation)
│   ├── Dockerfile
│   ├── main.py
│   ├── config.yaml
│   └── automation/
│       └── irrigation.py
└── AGENTS.md
```

## Backup

```bash
ssh finca@192.168.1.114 'tar czf ~/Raspberry-v2/backup_bridge_$(date +%Y%m%d_%H%M%S).tar.gz -C ~/Raspberry-v2/bridge .'
```
