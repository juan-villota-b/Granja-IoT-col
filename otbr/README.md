# OTBR — OpenThread Border Router + ThingsBoard Edge + Bridge

Infraestructura IoT profesional: **sensores Thread CoAP Client → Bridge CoAP Server → ThingsBoard Edge via MQTT**.

## Arquitectura Final

```
                          WiFi / Ethernet
  ┌──────────────────────────────────────────────────────────────┐
  │  HOST (Ubuntu 24.04)                                         │
  │                                                              │
  │  ┌──────────────────────────────────────────────────────┐   │
  │  │  Docker Compose                                       │   │
  │  │                                                       │   │
  │  │  ┌──────────┐   ┌─────────────────┐   ┌───────────┐  │   │
  │  │  │  OTBR    │   │  TB Edge        │   │  Bridge    │  │   │
  │  │  │  :8083   │   │  :8082 (web)    │   │  CoAP Svr  │  │   │
  │  │  │  :8081   │   │  :1884 (MQTT)   │   │  :5685     │  │   │
  │  │  └────┬─────┘   └────────┬────────┘   └─────┬──────┘  │   │
  │  │       │                  │                   │         │   │
  │  │       │     wpan0        │     MQTT:1884     │         │   │
  │  │       │◄══════════════════┘                   │         │   │
  │  └───────┼───────────────────────────────────────┼─────────┘   │
  └──────────┼───────────────────────────────────────┼─────────────┘
             │                                       │
     Thread 802.15.4                         ┌──────┴──────┐
             │                               │  Postgres   │
    ┌────────┴────────┐                      │  :5432      │
    │  ESP32-C6       │                      └─────────────┘
    │  CoAP Client    │
    │  (Router FTD)   │
    │                 │
    │  loop 30s:      │
    │   leer sensores │
    │   empaquetar    │
    │   CBOR          │
    │   POST /readings│
    │   → Bridge:5685 │
    └─────────────────┘
```

**Flujo de datos:**
```
ESP32-C6 ──POST CBOR──→ Bridge:5685 ──MQTT──→ TB Edge:1884 ──Web──→ :8082
  {id, t, h, b, r, u}    decode+publish     telemetry topic      dashboard
```

## Servicios (docker-compose)

| Servicio | Puerto | Descripción |
|----------|--------|-------------|
| Granja Dashboard | 3000 | FastAPI + Leaflet + Chart.js — dashboard a medida |
| OTBR | 8083 (web), 8081 (REST) | OpenThread Border Router — rutea IPv6 Thread ↔ WiFi |
| ThingsBoard Edge | 8082 (web), 1884 (MQTT) | Plataforma IoT — dashboards, telemetría, RPC |
| PostgreSQL | - (interno) | Base de datos de TB Edge |
| Bridge CoAP Server | **5685** (UDP) | Servidor CoAP — recibe POSTs de nodos, publica MQTT |

## URLs de acceso

| URL | Qué es |
|-----|--------|
| `https://<nombre>.tailXXXXX.ts.net` | **Dashboard público** (Tailscale Funnel) — accesible desde internet |
| `http://localhost:3000` | Granja Dashboard — mapa con sensores, gráficas, control de válvula (local) |
| `http://localhost:8082` | ThingsBoard Edge — dashboards |
| `http://localhost:8083` | OTBR Web GUI — monitoreo Thread |
| `http://localhost:8081` | OTBR REST API |

## Puesta en marcha

### 1. Levantar los servicios

```bash
cd ~/Documentos/Semestre_VII/Granja-IOT/otbr
./start.sh                 # todo con rebuild forzado
./start.sh granja-dashboard  # solo el dashboard
```

> **NUNCA usar `docker compose up` sin `--build`** — la imagen cacheada puede estar desactualizada y servir código viejo.
> `./start.sh` siempre fuerza `--build` y además aplica automáticamente la regla ip6tables del paso 3.

### 2. Verificar que todo está corriendo

```bash
docker ps --filter "name=otbr|iot-bridge|mytbedge"
docker logs iot-bridge --tail 20
```

### 3. Agregar regla de firewall (IMPORTANTE)

El UFW bloquea tráfico UDP entrante desde la red Thread. Agregar regla:

```bash
docker exec otbr ip6tables -I INPUT 1 -p udp --dport 5685 -j ACCEPT
```

> `./start.sh` aplica esta regla automáticamente después de levantar los servicios. No hace falta ejecutarla manualmente cada vez.

### 4. Flashear los ESP32-C6 con el firmware SED (ultra bajo consumo)

```bash
# Nodo 1 — NODO-TH-AUTO (ZONA-A)
cd ~/Documentos/Semestre_VII/Granja-IOT/nodo-coap-sed
. ~/.espressif/v5.5.4/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM1 flash

# Nodo 2 — NODO-TH-AUTO-2 (ZONA-B)
cd ~/Documentos/Semestre_VII/Granja-IOT/nodo-coap-sed-2
. ~/.espressif/v5.5.4/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM2 flash
```

> El `sdkconfig` tiene `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` habilitado para debugging. Para producción (SED real), usar `SDKCONFIG_DEFAULTS=sdkconfig.sed` que desactiva la consola y baja el CPU a 80MHz.

### 5. Monitorear

```bash
# Logs del bridge (datos de telemetría de todos los nodos)
docker logs -f iot-bridge

# Logs del OTBR (estado Thread)
docker logs -f otbr

# Logs del dashboard (API + WebSocket)
docker logs -f granja-dashboard

# Datos en ThingsBoard
# Abrir http://localhost:8082 → Devices → NODO-TH-AUTO → Latest Telemetry

# Dashboard a medida
# Abrir http://localhost:3000 → login con credenciales TB → mapa + gráficas
```

## Comandos útiles OTBR

```bash
# Estado de la red Thread
docker exec otbr ot-ctl state
docker exec otbr ot-ctl router table
docker exec otbr ot-ctl child table
docker exec otbr ot-ctl eidcache
docker exec otbr ot-ctl neighbor table

# Parámetros de red
docker exec otbr ot-ctl networkname
docker exec otbr ot-ctl channel
docker exec otbr ot-ctl panid
docker exec otbr ot-ctl extpanid

# Dataset activo (hex)
docker exec otbr ot-ctl dataset active -x

# Ping a un nodo
docker exec otbr ot-ctl ping fd29:c51e:a87a:e5e5:b5ff:a29a:bcd3:efd1

# Entrar al contenedor
docker exec -it otbr bash
```

## Bridge — Cómo funciona

El bridge expone un **servidor CoAP en `[::]:5685`** con un recurso:

| URI | Método | Payload | Respuesta |
|-----|--------|---------|-----------|
| `/readings` | POST | CBOR `{id, t, h, b, r, u}` | CBOR `{v: 0/1}` (comando válvula) |

### Payload CBOR de telemetría (POST desde el nodo)

```cbor
A6                    # map(6)
  61 69 64            # "id"
  6D 4E4F44...        # "NODO-TH-AUTO" (13 bytes)
  61 74               # "t"
  F9 HH LL             # half-float temperatura
  61 68               # "h"
  18 HH                # uint8 humedad
  61 62               # "b"
  19 HH LL             # uint16 batería (mV)
  61 72               # "r"
  38 HH                # negint RSSI (dBm)
  61 75               # "u"
  1A HH HH HH HH       # uint32 uptime (s)
```

### Respuesta del bridge (piggyback downlink)

```cbor
A0          # {}  → sin comando pendiente
A2 61 76 01 # {"v":1} → abrir válvula
A2 61 76 00 # {"v":0} → cerrar válvula
```

## Granja Dashboard (FastAPI + Leaflet + Chart.js)

Dashboard web a medida en **puerto 3000** con autenticación contra ThingsBoard.

### Características

- **Mapa Leaflet interactivo** con sensores Thread posicionados por atributos `lat`/`lng` o `pos_x`/`pos_y`
- **Estado de la red** en la barra superior: total de nodos, activos, inactivos
- **Conexiones visuales** entre Gateway y sensores (líneas punteadas violeta)
- **Gráficas en tiempo real** (WebSocket): temperatura, humedad, batería
- **SPA navigation**: Dashboard, Históricos (con exportación CSV), Válvula (RPC), Reglas
- **Tema claro/oscuro** (toggle en sidebar, persistido en localStorage)
- **El mapa recuerda zoom y posición** al recargar la página (localStorage `granja_map_state`)
- **Custom select** con indicador de batería y status activo/inactivo por nodo

### Backend

- **Python FastAPI** con Jinja2 templates server-side
- **Autenticación** vía session token httponly contra TB REST API
- **WebSocket** con broadcast cada 5s para telemetría en vivo
- **API REST**: `/api/devices`, `/api/telemetry/:id/history`, `/api/rpc/valve`

### Acceso

```
http://localhost:3000  → login con credenciales de ThingsBoard
http://localhost:3000/dashboard  → dashboard principal (requiere sesión)
```

## Acceso público por internet (Tailscale Funnel)

Expone el dashboard en una URL pública permanente usando **Tailscale Funnel** — gratis, con HTTPS automático y cifrado WireGuard extremo a extremo.

### Prerequisitos

- Dashboard funcionando en `http://localhost:3000`
- Cuenta en [login.tailscale.com](https://login.tailscale.com) (Google/GitHub)

### Instalación paso a paso

**1. Instalar Tailscale**

```bash
curl -fsSL https://tailscale.com/install.sh | sh
```

**2. Autenticar**

```bash
sudo tailscale up
```

Se abre el navegador — inicia sesión con Google o GitHub.

**3. Exponer el dashboard al internet**

```bash
sudo tailscale serve --bg http://localhost:3000
sudo tailscale funnel --bg 3000
```

**4. URL pública**

Revisar con:

```bash
sudo tailscale funnel status
```
```
# Funnel on:
#     - https://NOMBRE.tailXXXXX.ts.net
```

Compartir ese link — cualquier persona con él puede ver el dashboard.

### Cambiar el nombre de la máquina (opcional)

```bash
# Cambiar desde la terminal
sudo tailscale up --hostname=granja-iot

# O desde login.tailscale.com → Machines → {nombre} → Rename
```

Luego reactivar serve + funnel:

```bash
sudo tailscale funnel --https=443 off   # limpiar viejo
sudo tailscale serve --https=443 off
sudo tailscale serve --bg http://localhost:3000
sudo tailscale funnel --bg 3000
```

### Auto-start al reiniciar el PC

Tailscaled ya viene configurado para arrancar con systemd. Para que el Funnel también se reactive automáticamente:

```bash
# Crear el script
sudo tee /usr/local/bin/tailscale-funnel-start.sh << 'EOF'
#!/bin/bash
for i in $(seq 1 30); do
    if tailscale status 2>/dev/null | grep -q "active\|idle"; then
        break
    fi
    sleep 2
done
tailscale serve --bg http://localhost:3000 2>/dev/null
tailscale funnel --bg 3000 2>/dev/null
EOF

sudo chmod +x /usr/local/bin/tailscale-funnel-start.sh

# Crear el servicio systemd
sudo tee /etc/systemd/system/tailscale-funnel.service << 'EOF'
[Unit]
Description=Tailscale Funnel para Granja Dashboard
After=network-online.target tailscaled.service docker.service
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/tailscale-funnel-start.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

# Activar
sudo systemctl daemon-reload
sudo systemctl enable tailscale-funnel.service
sudo systemctl start tailscale-funnel.service
```

---

## Directorios del proyecto

```
otbr/
├── docker-compose.yml    # Servicios Docker
├── start.sh              # Entrypoint: siempre --build + ip6tables
├── granja-dashboard/     # FastAPI dashboard (puerto 3000)
│   ├── Dockerfile
│   ├── app/
│   │   ├── static/js/    # map.js, realtime.js, historicos.js, valve.js, app.js
│   │   ├── static/css/   # style.css (tema tierra + claro)
│   │   ├── templates/    # base.html, dashboard.html, login.html
│   │   ├── main.py       # API FastAPI
│   │   ├── tb_client.py  # Cliente HTTP ThingsBoard
│   │   └── config.py
│   └── requirements.txt
├── bridge/               # Bridge Python (CoAP Server + MQTT)
│   ├── main.py           # Servidor CoAP /readings + MQTT publisher
│   ├── config.yaml       # Configuración MQTT, puertos
│   ├── mqtt/             # Publisher y subscriber MQTT
│   ├── start.sh          # Entrypoint del contenedor
│   └── Dockerfile
└── README.md
```

```
Granja-IOT/
├── Nodo-TH-auto/          # Firmware original (CoAP server) — OBSOLETO
├── nodo-copia-cliente/    # Firmware híbrido (FTD/SED) — EN DESUSO
├── nodo-coap-sed/         # Firmware SED nodo 1 (NODO-TH-AUTO, ZONA-A)
│   └── ...
└── nodo-coap-sed-2/       # Firmware SED nodo 2 (NODO-TH-AUTO-2, ZONA-B)
    ├── main/
    │   ├── nodo_th_auto.c  # Punto de entrada SED
    │   ├── coap_client.c   # CoAP client + should_send() + CBOR encoder
    │   ├── node_config.h   # NODE_ID=NODO-TH-AUTO-2, ZONE_ID=ZONA-B
    │   └── ...
    ├── sdkconfig.defaults
    ├── sdkconfig.sed
    └── README.md
```

## Stack de protocolos

```
Aplicación:  CoAP (CBOR) — UDP :5685
Transporte:  UDP
Red:         IPv6 (Thread Mesh-Local fd29::/64)
Enlace:      6LoWPAN + IEEE 802.15.4 (canal 17)
Físico:      ESP32-C6 radio nativa 2.4 GHz
```

## ThingsBoard Edge — Cómo funciona

TB Edge es la plataforma IoT que recibe, almacena y visualiza los datos de los sensores.
Corre localmente en Docker y se comunica con el bridge vía **MQTT usando la Gateway API**.

### Modelo Gateway

```
  NODO-TH-AUTO            ThingsBoard Edge
  (ESP32-C6)                  (Docker)
      │                           │
      │  ┌────────────────────┐   │
      │  │  IoT-Gateway       │   │  ← dispositivo gateway (creado manualmente)
      │  │  access_token: X   │   │
      │  └────────┬───────────┘   │
      │           │               │
      └───CoAP──→ Bridge ──MQTT──→ TB Edge
                    │               │
                    │  v1/gateway/  │
                    │  connect      │  → auto-crea NODO-TH-AUTO
                    │  telemetry    │  → almacena datos cada 30s
                    └───────────────┘
```

**El Bridge actúa como Gateway MQTT** de ThingsBoard. Eso significa que:

1. **El Gateway (`IoT-Gateway`) se crea UNA vez** manualmente en TB Edge con la opción ☑ Is gateway
2. **Los nodos se auto-crean** automáticamente la primera vez que el bridge publica `v1/gateway/connect`
3. El bridge **no necesita saber los nodos de antemano** — cada POST `/readings` nuevo crea su dispositivo

### Flujo temporal de una muestra de telemetría

```
 t=0s     ESP32-C6 despierta, lee sensores (temp=25.8, hum=56)
 t=0.01s  Empaqueta CBOR: 6 claves si ya registrado (~39 B)
          o 11 claves si es primer POST (~77 B, con attrs)
 t=0.02s  Envía POST CON vía CoAP/UDP al Bridge :5685
          ── Thread 802.15.4 (~20ms) ──→ OTBR ──→ Bridge
 t=0.04s  Bridge recibe POST, decodifica CBOR
 t=0.05s  Bridge publica MQTT v1/gateway/telemetry → TB Edge
          + si es nuevo: v1/gateway/connect + v1/gateway/attributes
 t=0.06s  TB Edge almacena en PostgreSQL
 t=0.07s  Bridge responde 2.05 Content {} (o con comando si hay)
          ←── Thread ──← OTBR ──← Bridge
 t=0.08s  ESP32-C6 recibe ACK → marca _g_registered = true
 t=0.08s  ESP32-C6 entra en vTaskDelay(30000) → espera 30s
```

**Latencia total: ~80 ms** desde sensor hasta base de datos.

### Registro del nodo (primer POST)

El firmware usa una bandera `_g_registered` para saber si ya envió atributos:

```
Boot ESP32 → _g_registered = false
  │
  ├── POST (77 B, 11 claves): id, t, h, b, r, u + zn, tp, x, y, v
  │     ↓
  │   Bridge responde ACK
  │     ↓
  │   _g_registered = true → "REGISTRO OK" en serial
  │
  └── siguientes POSTs (39 B, 6 claves): id, t, h, b, r, u
```

**Robustez:** Si el bridge está caído durante el primer POST:
- No hay ACK → `_g_registered` sigue `false`
- Siguiente POST (30s después) reintenta con los 11 claves completos
- Se repite hasta que el bridge confirme

### Topics MQTT usados

| Topic | Quién publica | Formato | Cuándo |
|-------|--------------|---------|--------|
| `v1/gateway/connect` | Bridge | `{"device":"NODO-TH-AUTO","type":"Sensor"}` | Solo la 1ª vez que ve el nodo |
| `v1/gateway/attributes` | Bridge | `{"NODO-TH-AUTO":{"zone":"ZONA-A","type":"th_auto",...}}` | 1ª vez + si cambian |
| `v1/gateway/telemetry` | Bridge | `{"NODO-TH-AUTO":[{"ts":1717...,"values":{...}}]}` | Cada 30s |
| `v1/gateway/rpc` | TB Edge → Bridge | `{"device":"Thread NODO-TH-AUTO","data":{...}}` | Cuando se envía comando |
| `v1/gateway/disconnect` | Bridge | `{"device":"NODO-TH-AUTO"}` | Si el nodo se desconecta |

### Payload de telemetría MQTT (lo que recibe TB Edge)

```json
{
  "NODO-TH-AUTO": [
    {
      "ts": 1717462800000,
      "values": {
        "temperature": 25.8,
        "humidity": 56,
        "battery": 3100,
        "rssi": -65,
        "uptime": 1721
      }
    }
  ]
}
```

- `ts` = timestamp en milisegundos Unix (el bridge lo genera con `time.time()*1000`)
- Puede enviar múltiples muestras en un solo mensaje (el array permite batching)
- Si hay varios nodos, se envían en el mismo JSON como keys adicionales

### Configuración paso a paso de TB Edge

**1. Acceder**
```
http://localhost:8082
Login: tenant@thingsboard.org / tenant
```

**2. Crear el Gateway Device**
- Sidebar → **Devices** → botón **+** → **Add new device**
- Name: `IoT-Gateway`
- ☑ **Is gateway** (OBLIGATORIO — habilita la Gateway API)
- Clic **Add**
- **IMPORTANTE:** en el popup de credenciales, copiar el **Access token**
- Pegar ese token en `bridge/config.yaml` → `mqtt.username`

**3. Ver telemetría en tiempo real**
- Devices → clic en `NODO-TH-AUTO` → pestaña **Latest Telemetry**
- Los datos aparecen automáticamente cada ~30s
- Columnas: `temperature`, `humidity`, `battery`, `rssi`, `uptime`

**4. Crear un Dashboard**
- Sidebar → **Dashboards** → **+** → **Create new dashboard**
- Nombre: `Monitoreo Granja`
- **Open dashboard** → **+ Add widget**
- Seleccionar: **Cards** → **Timeseries table** o **Chart**
- Data source: `NODO-TH-AUTO` → keys: `temperature`, `humidity`
- Clic **Add** → los datos aparecen en tiempo real

**5. Downlink (enviar comando al nodo)**
- Devices → `NODO-TH-AUTO` → pestaña **RPC**
- **+** → **Add RPC**
- Method: `set_valve` | Params: `{"state": 1}` (1=abrir, 0=cerrar)
- El bridge recibe el RPC y lo encola para el siguiente POST del nodo
- El nodo recibe `{"v": 1}` en la respuesta CoAP del bridge

### Cómo TB Edge estructura los datos

```
Tenant (tenant@thingsboard.org)
 └── Device: IoT-Gateway (☑ is gateway)
      ├── Device: NODO-TH-AUTO (auto-creado)
      │   ├── Telemetry (time-series, cada 30s):
      │   │     temperature, humidity, battery, rssi, uptime
      │   ├── Attributes (estáticos, 1ª vez + si cambian):
      │   │     zone, type, pos_x, pos_y, version
      │   └── RPC (downlink):
      │         set_valve(v), set_thresholds(tt, ht, hb)
      └── Device: NODO-TH-AUTO-2 (auto-creado cuando llegue otro nodo)
```

**Diferencia Telemetry vs Attributes:**
- **Telemetry** = datos que cambian con el tiempo (sensor, batería) → se grafican
- **Attributes** = datos que NO cambian o cambian rara vez (zona, posición) → filtros y metadata

### Configurar el nodo desde la GUI de TB Edge

El bridge soporta **RPC (Remote Procedure Call)** para enviar comandos al nodo. El flujo es:

```
TB Edge GUI → MQTT v1/gateway/rpc → Bridge → encola comando
                                                   ↓
Nodo ──POST /readings──→ Bridge ──ACK con comando──→ Nodo ejecuta
```

**Comandos disponibles:**

| RPC Method | Params | Qué hace | Cuándo se entrega |
|-----------|--------|---------|-------------------|
| `set_valve` | `{"state": 0/1}` | Abre/cierra válvula | En el próximo POST |
| `set_thresholds` | `{"tt":2.0,"ht":5,"hb":60}` | Cambia umbrales | En el próximo POST |

**Cómo enviar un RPC desde TB Edge:**
1. Devices → `NODO-TH-AUTO` → pestaña **RPC**
2. **+** → **Add RPC**
3. Method: `set_valve` | Params: `{"state": 1}`
4. El bridge recibe el comando y lo guarda
5. Cuando el nodo haga su siguiente POST (~30s), recibe `{"v": 1}` en la respuesta
6. El log del bridge muestra: `DOWNLINK NODO-TH-AUTO → {'v': 1}`

### Solución de problemas TB Edge

| Síntoma | Causa | Solución |
|---------|-------|---------|
| `MQTT: Not authorized` | Token incorrecto o device no es gateway | Revisar ☑ Is gateway en el device |
| Dispositivo no aparece | `connect_dev` no se ejecutó | Esperar el primer POST del nodo |
| Telemetría vacía | El token no coincide | Verificar `config.yaml` → `mqtt.username` |
| TB Edge no carga | Postgres no listo | `docker logs otbr-postgres-1` |

## Solución de problemas

### El bridge no recibe POSTs

```bash
# 1. Verificar que el nodo está en la red
docker exec otbr ot-ctl router table

# 2. Verificar que la regla de firewall existe
docker exec otbr ip6tables -L INPUT -n | grep 5685

# 3. Si no existe, agregarla
docker exec otbr ip6tables -I INPUT 1 -p udp --dport 5685 -j ACCEPT

# 4. Capturar tráfico para debug
docker exec otbr tshark -i wpan0 -f "udp port 5685"
```

### El nodo no se une a la red

```bash
# Verificar que nombre de red, canal, PAN ID coinciden
docker exec otbr ot-ctl networkname   # debe ser: OpenThread-5eac
docker exec otbr ot-ctl channel       # debe ser: 17

# Si cambió, actualizar BRIDGE_IPV6 en node_config.h y recompilar
```

### Puerto en uso

```bash
# Si :5683 está ocupado (TB Edge), usar :5685
# El firmware y el bridge deben coincidir en el puerto
grep COAP_SERVER_PORT nodo-coap-sed/main/node_config.h
grep "bind.*5685" bridge/main.py

### El SED no se conecta

```bash
# Verificar que el sdkconfig tenga MTD en vez de FTD
grep OPENTHREAD_MTD nodo-coap-sed/sdkconfig
# Si no: rm -f sdkconfig && SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.sed" idf.py reconfigure

# Verificar reinicios constantes (uptime se resetea)
# Causa posible: esp_pm_configure falla o consola deshabilitada
# Solución: mantener CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```
```
