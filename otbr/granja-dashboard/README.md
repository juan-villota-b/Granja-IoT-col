# Granja Dashboard

Dashboard IoT para monitoreo agrícola con sensores Thread conectados a ThingsBoard. Visualización en tiempo real de temperatura, humedad, batería, RSSI y uptime de nodos sensores desplegados en campo, con control remoto de válvulas de riego.

## Arquitectura

```
┌──────────────┐     HTTP/WS      ┌─────────────────┐     REST API      ┌──────────────┐
│  Navegador   │ ◄──────────────► │  FastAPI (:3000) │ ◄──────────────► │ ThingsBoard  │
│  (SPA vanilla)│                  │  Python backend  │                  │  (:8080)     │
└──────────────┘                  └─────────────────┘                  └──────────────┘
                                         │
                                    ┌────┴────┐
                                    │ Session │  (memoria)
                                    │  Store  │
                                    └─────────┘
```

- **Frontend**: HTML5 + CSS3 + Vanilla JS (SPA sin frameworks)
- **Backend**: FastAPI (Python) — proxy autenticado hacia ThingsBoard
- **Mapa**: Leaflet 1.9 + OpenStreetMap
- **Gráficos**: Chart.js 4.4 (tiempo real + históricos)
- **Tiempo real**: WebSocket (servidor → cliente, poll cada 5s a ThingsBoard)
- **IoT**: ThingsBoard CE/Edge como backend de telemetría

---

## Estructura del proyecto

```
granja-dashboard/
├── Dockerfile
├── requirements.txt
├── start.sh
├── README.md
└── app/
    ├── __init__.py
    ├── config.py          # Variables de entorno (TB_HOST, APP_PORT, etc.)
    ├── auth.py            # JWT session tokens (HS256, 24h)
    ├── tb_client.py       # Cliente HTTP asíncrono para ThingsBoard REST API
    ├── main.py            # FastAPI: rutas, WebSocket, poll loop
    ├── templates/
    │   ├── base.html      # Shell: sidebar, navegación SPA, SVG icons
    │   ├── login.html     # Página de login independiente
    │   └── dashboard.html # Dashboard: mapa + panel de telemetría
    └── static/
        ├── css/
        │   └── style.css  # Tema granja (oscuro/claro), responsive
        └── js/
            ├── app.js         # Core: estado, router SPA, API, toasts, custom selects
            ├── map.js         # Leaflet: marcadores, gateway, líneas de conexión
            ├── realtime.js    # WebSocket + Chart.js sparklines
            ├── historicos.js  # Consultas históricas, gráfico, tabla, CSV
            └── valve.js       # Control de válvulas vía RPC
```

---

## Flujo de autenticación

1. Usuario ingresa credenciales en `/login`
2. `POST /api/login` → FastAPI autentica contra ThingsBoard (`POST /api/auth/login`)
3. ThingsBoard devuelve un **token JWT de TB**
4. FastAPI lo almacena en `_session_store` (dict en memoria) y emite su propio **JWT de sesión** (HS256, 24h, httpOnly cookie)
5. Requests subsiguientes: FastAPI extrae el session token de la cookie, busca el TB token asociado, y lo usa para consultar ThingsBoard

```
POST /api/login {username, password}
  → tb.login(username, password)
    → POST {TB}/api/auth/login
      ← {token: "tb_jwt_..."}
  ← set_cookie(session_token=fastapi_jwt)
```

### Archivos clave

| Archivo | Rol |
|---------|-----|
| `auth.py` | `create_session_token(username)` — genera JWT con `{username, exp}` |
| `auth.py` | `decode_session_token(token)` — verifica/decodifica |
| `main.py:24-35` | `_session_store` — dict `{session_token: tb_token}` protegido por `asyncio.Lock` |
| `main.py:60-83` | `POST /api/login` — login completo |
| `main.py:86-94` | `POST /api/logout` — borra sesión y cookie |

---

## API de dispositivos

### `GET /api/devices`

Filtra los dispositivos del tenant de ThingsBoard:

1. `tb.get_tenant_devices(token)` → `GET {TB}/api/tenant/devices?pageSize=100`
2. Filtra por nombre: solo dispositivos que **no** se llamen `Gateway-asus` y cuyo nombre empiece con `Thread` o `NODO`
3. Para cada dispositivo filtrado, obtiene atributos y telemetría más reciente

```python
# main.py:99-129
for dev in all_devices:
    name = dev.get("name", "")
    if name != gateway and not name.startswith("Thread") and not name.startswith("NODO"):
        continue  # solo Thread nodes + gateways (excepto Gateway-asus)
    attrs = await tb.get_device_attributes(tb_token, dev_id)
    telemetry = await tb.get_latest_telemetry(tb_token, dev_id)
    result.append({"id": dev_id, "name": name, "type": ..., "attributes": attrs, "telemetry": telemetry})
```

### APIs auxiliares

| Endpoint | Método TB usado | Descripción |
|----------|----------------|-------------|
| `GET /api/telemetry/{id}` | `GET /api/plugins/telemetry/DEVICE/{id}/values/timeseries` | Último valor de cada key |
| `GET /api/telemetry/{id}/history?keys=&startTs=&endTs=&agg=AVG&interval=` | Mismo endpoint + params | Serie temporal agregada |
| `GET /api/attributes/{id}` | `GET /api/plugins/telemetry/DEVICE/{id}/values/attributes` | Atributos del dispositivo |
| `POST /api/rpc/valve` | `POST /api/rpc` | Envía RPC `set_valve` al dispositivo |

---

## ThingsBoard REST API — tb_client.py

El archivo `app/tb_client.py` encapsula todas las llamadas a ThingsBoard:

### Autenticación
```python
async def login(username, password) -> Optional[str]:
    POST {base}/api/auth/login
    Body: {"username": ..., "password": ...}
    Returns: token JWT de ThingsBoard
```

### Dispositivos
```python
async def get_tenant_devices(token, page_size=100) -> list[dict]:
    GET {base}/api/tenant/devices?pageSize=100&page=0
    Header: X-Authorization: Bearer {token}
    Returns: data[] con {id: {entityType, id}, name, type, label, ...}
```

### Telemetría
```python
async def get_latest_telemetry(token, device_id, keys="") -> dict:
    GET {base}/api/plugins/telemetry/DEVICE/{id}/values/timeseries?keys=...
    Returns: {temperature: "25.5", humidity: "60", ..., _ts: "1717459200000"}
    # _ts es el timestamp del dato más reciente

async def get_telemetry_history(token, device_id, keys, start_ts, end_ts, agg, interval, limit=1000) -> dict:
    GET {base}/api/plugins/telemetry/DEVICE/{id}/values/timeseries
        ?keys=temperature&startTs=...&endTs=...&agg=AVG&interval=3600000&limit=1000
    Returns: {temperature: [{ts, value}, ...]}
    # Con aggregación, TB Edge 4.2 incluye aggValues: {MIN, MAX, AVG, COUNT, SUM}
```

### Atributos
```python
async def get_device_attributes(token, device_id) -> dict:
    GET {base}/api/plugins/telemetry/DEVICE/{id}/values/attributes
    Returns: {key: value, ...}  # ej: {lat: 5.07, lng: -75.52, zone: "Invernadero 1"}
```

### RPC (Remote Procedure Call)
```python
async def send_rpc(token, device_id, method, params) -> bool:
    POST {base}/api/rpc
    Body: {"method": "set_valve", "params": {"state": 1}}
    Returns: True si HTTP 200
```

---

## Tiempo real — WebSocket + Poll Loop

### Servidor (main.py:200-247)

```python
connected_websockets: dict[str, list[WebSocket]] = {}  # device_id → [ws1, ws2, ...]

@app.websocket("/ws/{device_id}")
async def websocket_endpoint(websocket, device_id):
    # Cliente se conecta, queda en loop escuchando (keepalive)

async def poll_and_broadcast():
    while True:
        for session_token, tb_token in sessions:
            for dev in devices:
                telemetry = await tb.get_latest_telemetry(...)
                for ws in connected_websockets[dev_id]:
                    await ws.send_text(json.dumps(telemetry))
        await asyncio.sleep(5)  # cada 5 segundos
```

### Cliente (realtime.js)

1. `startRealtimeCharts(deviceId)` — crea 3 Chart.js sparklines (temp naranja, humedad cyan, batería verde)
2. Abre `WebSocket` a `ws://host/ws/{deviceId}`
3. `onmessage`: agrega punto al chart, actualiza popup del marcador en el mapa, refresca info del nodo
4. Deduplicación: no agrega puntos idénticos en < 30 segundos
5. Máximo 60 puntos (rolling window)
6. **Reconexión automática**: si el WS se cae, reintenta cada 3 segundos

---

## Frontend — Arquitectura JS

### Namespace `App` (app.js)

El estado de la aplicación está centralizado en un IIFE:

```javascript
const App = (() => {
  const state = {
    devices: [],        // lista de dispositivos cargados
    activeNodeId: null, // nodo seleccionado actualmente
    currentPage: 'dashboard',
  };

  // Métodos públicos
  return {
    state,
    api(endpoint, options),    // fetch wrapper con auth automática
    toast(message, type),      // notificaciones toast
    esc(str),                  // sanitización HTML (previene XSS)
    formatUptime(seconds),     // 3661 → "1h 1m"
    batteryPercent(mV),        // 3700mV → 58% (Li-Ion 3000-4200mV)
    isDeviceActive(dev),       // telemetría < 5 min?
    isGateway(dev),            // nombre contiene "gateway"?
    loadDevices(),             // GET /api/devices → state.devices
    selectNode(id),            // selecciona nodo, actualiza info + charts
    renderNodeInfo(dev),       // renderiza 6 cards de telemetría
    navigate(page),            // router SPA
    setup(),                   // inicializa eventos, tema, menú
  };
})();
```

### SPA Router (`navigate(page)`)

La app es una Single Page Application. Al hacer clic en el sidebar:

1. Fade out del contenido actual (150ms)
2. `innerHTML` con el markup de la nueva página
3. Inicializa los componentes específicos (mapa, charts, históricos, válvula)
4. Fade in (300ms)

No hay recarga de página completa en ningún momento.

### Custom Selects

Los `<select>` nativos se reemplazan por componentes personalizados con:
- Trigger estilizado con chevron animado
- Dropdown tipo cortina (`curtainDown` animation)
- Opciones con indicador de selección
- Sincronización bidireccional con el `<select>` nativo (accesibilidad)

---

## Mapa — map.js

### Capas

| Capa | Visibilidad | Contenido |
|------|------------|-----------|
| `gatewayGroup` | Siempre | Gateway hexagonal con ícono de antena |
| `nodeGroup` | Siempre | Círculos: verde (sensor), rojo (válvula), gris (inactivo) |
| `connectionLines` | Siempre | Líneas punteadas gateway → cada nodo |

### Coordenadas

Los nodos pueden usar coordenadas GPS reales (`lat`, `lng` como atributos) o un grid virtual 0-100 (`pos_x`, `pos_y`) mapeado a la región de Manizales (5.07, -75.52) con escala 0.002.

```javascript
// grid virtual → lat/lng
function toLatLng(posX, posY) {
  return [5.07 + (posY - 50) * 0.002, -75.52 + (posX - 50) * 0.002];
}
```

### Gateway

- **Icono**: hexágono púrpura (36px) con ícono SVG de antena Thread
- **Popup**: nombre, "Gateway Thread · Border Router", conteo de sensores activos/inactivos
- **Líneas de conexión**: polilíneas punteadas púrpuras (activo) o grises (inactivo)

### Actualización en tiempo real

`updateMarkerTelemetry(deviceId, telemetry)` se llama desde el WebSocket. Cambia el color, opacidad y pulso del marcador según el estado activo/inactivo del nodo.

---

## Históricos — historicos.js

### Vistas

| Vista | Rango | Intervalo de agregación | Puntos aprox. |
|-------|-------|------------------------|---------------|
| 24h | Últimas 24 horas | 10 minutos | ~144 |
| 30d | Últimos 30 días | 2 horas | ~360 |
| Custom | Fechas manuales | Configurable (1h-24h) | Variable |

### API call

```
GET /api/telemetry/{deviceId}/history
  ?keys=temperature
  &startTs=1717400000000
  &endTs=1717500000000
  &agg=AVG
  &interval=600000
```

### Componentes

- **Summary cards**: promedio, mínimo, máximo, total de registros
- **Gráfico**: Chart.js línea con tooltips y leyenda
- **Tabla paginada**: 100 registros por página, columnas avg/min/max reales desde `aggValues`
- **Exportar CSV**: botón que descarga los datos en formato CSV

### Parseo de respuesta TB

ThingsBoard Edge 4.2 devuelve datos agregados con este formato:
```json
{
  "temperature": [{
    "ts": 1717459200000,
    "value": "25.5",
    "aggValues": {
      "MIN": "24.0",
      "MAX": "26.5",
      "AVG": "25.3",
      "COUNT": "120",
      "SUM": "3036.0"
    }
  }]
}
```

El frontend extrae `aggValues.AVG`, `aggValues.MIN`, `aggValues.MAX` para cada fila.

---

## Válvula — valve.js

### Flujo

1. `checkValveNodes()` — busca dispositivos tipo "valve" en `App.state.devices`
2. Si encuentra uno, habilita los botones ABRIR/CERRAR y muestra el estado actual
3. `sendValveCommand(0|1)` → `POST /api/rpc/valve {device_id, state}`
4. Backend llama a `tb.send_rpc(token, device_id, "set_valve", {state})`
5. Feedback vía toast notification

La función usa `App.state.devices` (cache) y solo hace fetch si está vacío. El `_valveDeviceId` se cachea para no repetir búsquedas.

---

## Tema visual

### Paleta

| Variable | Modo oscuro | Modo claro | Uso |
|----------|------------|-----------|-----|
| `--bg` | `#171410` | `#faf7f0` | Fondo principal |
| `--surface` | `#1f1b16` | `#f0ebe0` | Sidebar, panel |
| `--card` | `#292420` | `#ffffff` | Tarjetas, charts |
| `--primary` | `#84cc16` | `#84cc16` | Verde lima (cultivo) |
| `--accent` | `#f59e0b` | `#f59e0b` | Ámbar (trigo) |
| `--water` | `#06b6d4` | `#06b6d4` | Cyan (agua) |
| `--text` | `#f5f0e0` | `#292420` | Texto principal |

### Toggle claro/oscuro

Botón ☀️ en la sidebar. Guarda preferencia en `localStorage.theme`. Los charts leen las variables CSS via `getComputedStyle()` al crearse.

### Responsive

- **> 1100px**: layout normal (sidebar 260px + mapa + panel 400px)
- **900-1100px**: panel reducido a 340px
- **< 900px**: sidebar horizontal, mapa 50vh, panel abajo, menú hamburguesa
- **< 500px**: grid de cards a 1 columna, tabs apilados

---

## Variables de entorno

| Variable | Default | Descripción |
|----------|---------|-------------|
| `TB_HOST` | `host.docker.internal` | Host de ThingsBoard |
| `TB_PORT` | `8080` | Puerto de ThingsBoard |
| `APP_PORT` | `3000` | Puerto de la app |
| `APP_SECRET` | `granja-dashboard-secret-key-change-in-production` | Clave para firmar JWT |

---

## Ejecución

### Docker (recomendado)

```bash
docker build -t granja-dashboard .
docker run -d --name granja-dashboard \
  -p 3000:3000 \
  -e TB_HOST=thingsboard \
  -e TB_PORT=8080 \
  granja-dashboard
```

### Desarrollo local

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
TB_HOST=localhost TB_PORT=8080 uvicorn app.main:app --host 0.0.0.0 --port 3000 --reload
```

Acceder a `http://localhost:3000` — login con credenciales de ThingsBoard.

---

## Seguridad

- **Session tokens**: JWT HS256, 24h expiración, cookies httpOnly + SameSite=Lax
- **TB tokens**: almacenados solo en memoria del servidor (nunca expuestos al cliente)
- **Sanitización HTML**: `App.esc()` escapa todo contenido dinámico (previene XSS)
- **API保护**: todas las rutas verifican session token antes de consultar ThingsBoard
- **CORS**: no expuesto — el frontend y backend comparten origen
