# AGENTS.md — Granja Dashboard (entorno de desarrollo PC)

## Levantar servicios

```bash
./start.sh                    # levanta el dashboard con rebuild
```

**NUNCA usar `docker compose up` sin `--build`** — la imagen cacheada puede estar desactualizada y servir codigo viejo.

## Parar servicios

```bash
docker compose down
```

## Puertos

| Servicio | Puerto | Descripcion |
|----------|--------|-------------|
| Granja Dashboard | 3000 | FastAPI + Leaflet + Chart.js |

El dashboard se conecta a TB CE corriendo en el host (`host.docker.internal:8080`) via REST API.

---

## Control de Riego Automatico (corre en la RPi)

El bridge, OTBR, TB Edge y el controlador de riego corren en la Raspberry Pi (`Raspberry-v2/`), no en este entorno de desarrollo.

### Arquitectura

```
ESP32 (Nodo-Humedad) ──CoAP/CBOR──→ Bridge (RPi) ──→ irrigation.feed()
                                                         │
                                             ┌───────────▼────────────┐
                                             │ IrrigationController    │
                                             │  - ultima telemetria    │
                                             │  - umbrales configurables│
                                             │  - cooldown 60s (ABRIR) │
                                             └───────────┬────────────┘
                                                         │ pending_commands
ESP32 (BOMBA) ←──CoAP Downlink── Bridge ─────────────────┘
```

### Archivos (en Raspberry-v2/)

| Archivo | Rol |
|---------|-----|
| `Raspberry-v2/bridge/automation/irrigation.py` | Controlador: logica de decision |
| `Raspberry-v2/bridge/main.py` | Integracion: import, instanciacion, feed |
| `Raspberry-v2/bridge/config.yaml` | Umbrales y `actuator_nid` |

### Despliegue a la RPi

```bash
# Copiar archivos del bridge
scp Raspberry-v2/bridge/automation/irrigation.py finca@192.168.1.114:~/Raspberry-v2/bridge/automation/
scp Raspberry-v2/bridge/automation/__init__.py finca@192.168.1.114:~/Raspberry-v2/bridge/automation/

# Reconstruir el contenedor en la RPi
ssh finca@192.168.1.114 'cd ~/Raspberry-v2 && docker compose up -d --build bridge'

# Ver logs
ssh finca@192.168.1.114 'docker logs -f iot-bridge | grep -E "AUTO|irrigation"'
```
