# AGENTS.md — OTBR + Granja Dashboard

## Levantar servicios

```bash
./start.sh                 # todo con rebuild
./start.sh granja-dashboard  # solo el dashboard
```

**NUNCA usar `docker compose up` sin `--build`** — la imagen cacheada puede estar desactualizada y servir código viejo.

## Parar servicios

```bash
docker compose down
```

## Puertos

| Servicio | Puerto | Descripción |
|----------|--------|-------------|
| Granja Dashboard | 3000 | FastAPI + Leaflet + Chart.js |
| TB Edge Web | 8082 | ThingsBoard Edge UI |
| TB Edge API | 8080 | ThingsBoard Edge REST API |
| OTBR Web | 8083 | OpenThread Border Router Web GUI |
| OTBR REST | 8081 | OpenThread Border Router REST API |
| MQTT | 1884 | TB Edge MQTT broker |
| CoAP Server | 5685/udp | Bridge CoAP (escucha de sensores) |
