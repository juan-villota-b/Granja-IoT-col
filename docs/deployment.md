# Despliegue

Procedimiento completo para levantar el sistema en un entorno de laboratorio: servidor central, dashboard, gateway de campo y nodos.

- [Requisitos](#requisitos)
- [1. Servidor ThingsBoard CE](#1-servidor-thingsboard-ce)
- [2. Dashboard](#2-dashboard)
- [3. Gateway de campo](#3-gateway-de-campo)
- [4. Firmware de los nodos](#4-firmware-de-los-nodos)
- [5. Registro del gateway en ThingsBoard](#5-registro-del-gateway-en-thingsboard)
- [Acceso remoto](#acceso-remoto)
- [Operación diaria](#operación-diaria)
- [Diagnóstico](#diagnóstico)

---

## Requisitos

| Componente | Versión / modelo | Uso |
|------------|------------------|-----|
| ESP-IDF | ≥ 5.5 | Compilación del firmware |
| Docker Engine + Compose v2 | reciente | Gateway, dashboard y ThingsBoard |
| Python | 3.12+ | Desarrollo local del bridge y del dashboard |
| Raspberry Pi 4 | Raspberry Pi OS 64-bit | Gateway de campo |
| nRF52840 | dongle USB con firmware RCP | Radio del border router |
| ESP32-C6 | 4 placas | Nodos |
| Red | IPv6 habilitado en la interfaz de infraestructura | 6LoWPAN |

---

## 1. Servidor ThingsBoard CE

```bash
cd infra/thingsboard-ce
docker compose up -d
```

- Consola: `http://localhost:8080`
- Credenciales iniciales: `tenant@thingsboard.org` / `tenant`
- El primer arranque tarda entre 1 y 2 minutos mientras inicializa PostgreSQL y las tablas.

Datos persistentes en los volúmenes `tb-data` y `tb-logs`. Para reiniciar desde cero:

```bash
docker compose down -v
```

---

## 2. Dashboard

```bash
cd dashboard
./scripts/compose-up.sh            # todos los servicios
./scripts/compose-up.sh dashboard  # solo el dashboard
```

Abrir `http://localhost:3000` e iniciar sesión con las credenciales de ThingsBoard.

> El script siempre construye con `--build`. Evita `docker compose up` sin rebuild: la imagen cacheada puede servir código viejo.

Para desarrollo sin Docker:

```bash
cd dashboard
./scripts/dev.sh
```

### Variables de entorno

| Variable | Default | Descripción |
|----------|---------|-------------|
| `TB_HOST` | `host.docker.internal` | Host de ThingsBoard |
| `TB_PORT` | `8080` | Puerto de ThingsBoard |
| `APP_SECRET` | valor de laboratorio | Clave de firma de las sesiones JWT |

### Crear un customer con su usuario

```bash
cd dashboard
python3 tools/create_customer.py
```

Crea el customer `Finca`, el usuario `juan@finca.com`, asigna el edge `Granja-Raspberry` y todos sus dispositivos.

---

## 3. Gateway de campo

En la Raspberry Pi:

```bash
git clone <repositorio> ~/Granja-IOT
cd ~/Granja-IOT/gateway
./scripts/up.sh
```

El script levanta el stack y aplica la regla `ip6tables` que permite el tráfico CoAP desde la malla Thread hacia el bridge.

### Servicios levantados

| Servicio | Contenedor | Puerto |
|----------|-----------|--------|
| OTBR | `otbr` | `8083` web · `8081` REST |
| ThingsBoard Edge | `gateway-mytbedge-1` | `8082` web · `1883` MQTT |
| PostgreSQL | `gateway-postgres-1` | interno `5432` |
| Bridge | `iot-bridge` | `5685/udp` CoAP |

### Persistir la regla de firewall

La regla `ip6tables` se pierde al reiniciar. Para hacerla permanente:

```bash
cd gateway
sudo ./scripts/install-otbr-iptables-service.sh
```

Instala la unidad `otbr-coap-ip6tables.service`, que espera al contenedor `otbr` y aplica la regla en cada arranque.

### Configuración del bridge

`gateway/bridge/config.yaml`:

```yaml
mqtt_mode: direct                 # direct | gateway
device_tokens:                    # tokens MQTT por dispositivo
  Nodo-Luz: "<token>"
tb_edge:
  url: http://<ip-thingsboard-ce>:8080
irrigation:
  soil_low_pct: 30
  soil_high_pct: 70
  actuator_nid: BOMBA
```

Tras cualquier cambio:

```bash
cd gateway
docker compose up -d --build bridge
```

---

## 4. Firmware de los nodos

### Preparar el entorno

```bash
. $IDF_PATH/export.sh
```

### Compilar y flashear

```bash
cd firmware/nodes/light-node
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Repetir en `temperature-node`, `soil-node` y `valve-node`, ajustando el puerto serie.

### Identidad de cada nodo

Antes de compilar, revisar `main/node_config.h`:

| Macro | Descripción |
|-------|-------------|
| `NODE_ID` | Nombre del dispositivo en ThingsBoard (`Nodo-Luz`, `BOMBA`, …) |
| `ZONE_ID` | Zona de cultivo |
| `PROV_KEY` | Access token de ThingsBoard del dispositivo |
| `LAT_DEFAULT` / `LNG_DEFAULT` | Posición en el mapa |
| `BRIDGE_IPV6` | Dirección anycast del border router |
| `BRIDGE_PORT` | Puerto CoAP del bridge (`5685`) |

### Configuración del build

`sdkconfig.defaults` fija el objetivo de bajo consumo: 80 MHz, `CONFIG_OPENTHREAD_MTD=y`, `CONFIG_PM_ENABLE=y` y *tickless idle*. No es necesario pasar un `SDKCONFIG_DEFAULTS` adicional.

---

## 5. Registro del gateway en ThingsBoard

Solo si `mqtt_mode: gateway`:

1. Abrir `http://<ip-raspberry>:8082` → **Devices** → **+** → **Add device**.
2. Nombre `IoT-Gateway`, marcar **Is gateway**.
3. Copiar el *access token*.
4. Pegarlo en `gateway/bridge/config.yaml` → `mqtt.username`.
5. Reconstruir: `docker compose up -d --build bridge`.

Con `mqtt_mode: direct` no se requiere gateway: cada nodo publica con su propio token.

---

## Acceso remoto

El dashboard se expone por HTTPS con **Tailscale Funnel**, sin abrir puertos en el router:

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
sudo tailscale serve --bg http://localhost:3000
sudo tailscale funnel --bg 3000
```

La URL pública la asigna Tailscale (`https://<equipo>.<tailnet>.ts.net`).

---

## Operación diaria

```bash
# Estado del stack de campo
cd gateway && docker compose ps

# Logs
docker logs -f iot-bridge                                  # bridge + riego
docker logs -f otbr                                        # border router
docker logs -f gateway-mytbedge-1                          # ThingsBoard Edge
docker logs -f iot-bridge | grep -E "AUTO|irrigation"      # solo decisiones de riego

# Detener
docker compose down
```

### Actualizar el bridge en la Raspberry

```bash
scp gateway/bridge/main.py <usuario>@<ip>:~/Granja-IOT/gateway/bridge/
scp -r gateway/bridge/irrigation <usuario>@<ip>:~/Granja-IOT/gateway/bridge/
ssh <usuario>@<ip> 'cd ~/Granja-IOT/gateway && docker compose up -d --build bridge'
```

### Respaldo

```bash
ssh <usuario>@<ip> 'tar czf ~/backup-bridge-$(date +%Y%m%d).tar.gz -C ~/Granja-IOT/gateway/bridge .'
```

---

## Diagnóstico

| Síntoma | Verificación |
|---------|--------------|
| El dashboard no carga datos | `curl http://localhost:8080/api/system/info` y `docker logs -f granja-dashboard` |
| Los nodos no aparecen | `docker logs -f iot-bridge` debe mostrar `>>> <NODO_ID> …`; si no hay POST, revisar `BRIDGE_IPV6` y la regla `ip6tables` |
| CoAP llega pero no se publica | Revisar el token del dispositivo en `device_tokens` y que `mqtt_mode` coincida con la configuración de ThingsBoard |
| El nodo consume demasiado | En el log de arranque buscar `SED mode: rx=0 devtype=0`; si `rx=1`, el modo no se aplicó |
| La válvula no responde | El downlink requiere que el nodo parsee el CBOR de respuesta (ver limitaciones en `architecture.md`) |
| Puerto 3000 ocupado | `sudo lsof -i :3000` y reiniciar el stack |
| ThingsBoard Edge no sincroniza | Verificar `CLOUD_RPC_HOST` y que el puerto `7070` del CE sea alcanzable desde la Raspberry |
