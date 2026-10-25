# Gateway de campo — Raspberry Pi 4

Stack local que conecta la malla Thread con ThingsBoard y ejecuta el control automático de riego. **Funciona sin internet**: el lazo de decisión y la base de datos viven en la propia Raspberry.

## Servicios

| Servicio | Contenedor | Puerto | Descripción |
|----------|-----------|--------|-------------|
| `otbr` | `otbr` | `8083` web · `8081` REST | OpenThread Border Router con RCP nRF52840 en `/dev/ttyACM0` (`network_mode: host`) |
| `mytbedge` | `gateway-mytbedge-1` | `8082` web · `1883` MQTT | ThingsBoard Edge 4.2.0EDGE |
| `postgres` | `gateway-postgres-1` | interno `5432` | Base de datos de ThingsBoard Edge |
| `bridge` | `iot-bridge` | `5685/udp` | Servidor CoAP, publisher MQTT y controlador de riego (`network_mode: host`) |

## Estructura

```
gateway/
├── docker-compose.yml            # otbr · mytbedge · postgres · bridge
├── bridge/
│   ├── main.py                   # Servidor CoAP /readings + orquestación
│   ├── config.yaml               # Tokens MQTT, ThingsBoard Edge, umbrales de riego
│   ├── mqtt/
│   │   ├── publisher.py          # Publicación de telemetría y atributos
│   │   └── subscriber.py         # Suscripción a RPC de ThingsBoard
│   ├── irrigation/
│   │   └── controller.py         # Lógica de apertura/cierre de la válvula
│   ├── Dockerfile
│   ├── entrypoint.sh
│   └── requirements.txt
├── scripts/
│   ├── up.sh                     # Levanta el stack y aplica la regla ip6tables
│   └── install-otbr-iptables-service.sh
└── README.md
```

## Arranque

```bash
cd gateway
./scripts/up.sh
```

El script hace `docker compose up -d --build` y aplica `ip6tables -I INPUT 1 -p udp --dport 5685 -j ACCEPT` dentro del contenedor `otbr`, necesario porque el bridge escucha en `[::]:5685` y la cadena `INPUT` descarta el tráfico por defecto.

Para que la regla sobreviva a un reinicio:

```bash
sudo ./scripts/install-otbr-iptables-service.sh
```

## Bridge

### Endpoint CoAP

| URI | Método | Payload | Respuesta |
|-----|--------|---------|-----------|
| `/readings` | POST | CBOR `{id, t\|h\|l\|v, r, u, ...}` | CBOR `{v: 0\|1}` o `{}` |

Cada POST puede traer:

1. **Aprovisionamiento:** `{prov_key, state: "pending"}` → el bridge resuelve el nombre del dispositivo en ThingsBoard.
2. **Telemetría:** claves cortas que el bridge traduce (`t → temperature`, `h → humidity`, `l → light`, `v → valve`, `b → battery`, `r → rssi`, `u → uptime`).
3. **Downlink:** si hay un comando pendiente para ese nodo, se adjunta en la respuesta.

### Modos MQTT

| Modo | Descripción |
|------|-------------|
| `direct` (activo) | Una conexión MQTT por dispositivo con su token en `device_tokens` |
| `gateway` | Una sola conexión de gateway; requiere marcar el dispositivo como *Is gateway* en ThingsBoard |

### Persistencia

Los nodos registrados se guardan en `/data/registered_nodes.json` (volumen `bridge-data`). El arranque siguiente reutiliza el mapeo `prov_key → dispositivo` sin consultar la API.

## Control automático de riego

`bridge/irrigation/controller.py` evalúa cada telemetría:

```yaml
irrigation:
  enabled: true
  soil_low_pct: 30        # abre por debajo de este valor
  soil_high_pct: 70       # cierra al alcanzarlo
  light_min_pct: 10       # no riega de noche
  temp_max_c: 35.0        # no riega en calor extremo
  min_cycle_seconds: 60   # anti-ciclado
  allowed_start_hour: 6
  allowed_end_hour: 23
actuator_nid: BOMBA
```

El comando se envía como RPC a ThingsBoard Edge **en la red local**; no depende de la nube.

## Operación

```bash
docker compose ps
docker logs -f iot-bridge                                # bridge y riego
docker logs -f otbr                                      # border router
docker logs -f gateway-mytbedge-1                        # ThingsBoard Edge
docker logs -f iot-bridge | grep -E "AUTO|irrigation"    # decisiones de riego
docker compose down
```

### Reconstruir solo el bridge

```bash
docker compose up -d --build bridge
```

## Configurar ThingsBoard Edge

1. Abrir `http://<ip-raspberry>:8082` e iniciar sesión (`tenant@thingsboard.org` / `tenant`).
2. **Devices → + → Add device**, nombre `IoT-Gateway`, marcar **Is gateway** (solo en modo `gateway`).
3. Copiar el *access token* en `bridge/config.yaml` → `mqtt.username`.
4. Reconstruir el bridge.

En modo `direct` cada nodo usa su propio token y no se necesita el dispositivo gateway.
