# AGENTS.md — Raspberry-v2 Gateway (RPi 4)

## Arquitectura

```
PC (192.168.1.207)                    RPi (192.168.1.114)
┌──────────────────────┐             ┌──────────────────────────┐
│  ThingsBoard CE      │ ← RPC ────  │  TB Edge + Postgres     │
│  :8080 / :7070       │             │  :8082 (web)             │
│                      │             │  :1883 (MQTT)            │
│  Granja Dashboard    │             │                          │
│  :3000               │             │  OTBR (host net)         │
└──────────────────────┘             │  wpan0 ← Thread 802.15.4 │
                                     │                          │
                                     │  Bridge CoAP (host net)  │
                                     │  [::]:5685 ← POSTs CBOR  │
                                     │     ↓ MQTT gateway       │
                                     │  → TB Edge :1883         │
                                     └──────────────────────────┘
```

## Servicios

| Servicio | Puerto | Descripción |
|----------|--------|-------------|
| OTBR Web | 8083 | OpenThread Border Router Web GUI |
| OTBR REST | 8081 | OpenThread Border Router REST API |
| TB Edge Web | 8082 | ThingsBoard Edge UI |
| TB Edge MQTT | 1883 | ThingsBoard Edge MQTT broker |
| Bridge CoAP | 5685/udp | CoAP server para sensores Thread |

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
docker logs -f iot-bridge        # Bridge CoAP
docker logs -f otbr              # OTBR
docker logs -f raspberry-v2-mytbedge-1  # TB Edge
```

## Datos de red

- PC (TB CE): `192.168.1.207:7070`
- RPi (TB Edge): `192.168.1.114:8082`
- Credenciales TB Edge: `tenant@thingsboard.org` / `tenant`

## Flujo de datos

```
ESP32-C6 ──POST CBOR──→ Bridge:5685 ──MQTT──→ TB Edge:1883
  {id, t, h, b, r, u}    decode+publish     telemetry topic
```

## Configurar gateway en TB Edge

1. Ir a http://192.168.1.114:8082
2. Devices → + → Add device
3. Name: `IoT-Gateway`, ☑ Is gateway
4. Copiar access token
5. Pegar token en `bridge/config.yaml` → `mqtt.username`
6. Rebuild: `docker compose up -d --build bridge`
