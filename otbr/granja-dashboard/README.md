# Granja Dashboard

Dashboard IoT para monitoreo agricola con sensores Thread conectados a ThingsBoard Edge. Mobile-first SPA con mapa, graficas en tiempo real, datos historicos, monitoreo unificado y control remoto de valvulas.

## Arquitectura

```
 ┌──────────────┐     HTTP       ┌─────────────────┐     REST API      ┌──────────────┐
 │  Navegador   │ ◄──────────────► │  FastAPI (:3000) │ ◄──────────────► │ TB Edge      │
 │  (SPA vanilla)│                  │  Python backend  │                  │  (:8080)     │
 │  Mobile-first│                  │  + WebSocket      │                  │  (host)      │
 └──────────────┘                  └─────────────────┘                  └──────────────┘
                                         │
                                    ┌────┴────┐
                                    │ Session  │  TB Edge en el host (localhost:8080)
                                    │ Store   │
                                    └─────────┘
```

- **Frontend**: vanilla JS SPA — mobile-first con bottom tab bar, drawer lateral
- **Backend**: FastAPI (Python) — proxy autenticado hacia ThingsBoard Edge
- **Mapa**: Leaflet 1.9, marcadores por tipo (verde=sensor, rojo=actuador)
- **Graficos**: Chart.js 4.4 — tiempo real (eje X lineal con scrolling) + historicos
- **IoT**: ThingsBoard Edge 4.2.0EDGE como backend de telemetria
- **Paginas**: Dashboard (mapa), Historicos (rango personalizado), Monitoreo (sensores + riego), Valvula (RPC), Agregar Nodo (minimapa)

---

## Estructura del proyecto

```
granja-dashboard/
├── Dockerfile
├── requirements.txt
├── setup_customer.py    # Script para crear customer + usuario en TB
├── README.md
└── app/
    ├── config.py          # Variables de entorno (TB_HOST, APP_PORT, etc.)
    ├── tb_client.py       # Cliente HTTP asincrono para ThingsBoard REST API
    ├── main.py            # FastAPI: rutas, WebSocket, endpoints
    ├── templates/
    │   ├── base.html      # Shell: sidebar, bottom nav, top bar mobile
    │   ├── login.html     # Pagina de login independiente
    │   └── dashboard.html # Dashboard: mapa + panel de telemetria
    └── static/
        ├── css/
        │   └── style.css  # Tema agricola (oscuro/claro), mobile-first
        └── js/
            ├── app.js          # Core: estado, router SPA, API, toasts, custom selects
            ├── map.js          # Leaflet: marcadores, gateway, conexiones, uptime
            ├── realtime.js     # Chart.js: ventana 1h, eje X lineal, WebSocket
            ├── historicos.js   # Consultas historicas, grafico, tabla, CSV
            ├── monitoreo.js    # Overlay 3 sensores + periodos de riego + crosshair
            └── valve.js        # Control de valvula via RPC
```

---

## Flujo de autenticacion

1. Usuario ingresa credenciales en `/login`
2. `POST /api/login` → FastAPI autentica contra ThingsBoard (`POST /api/auth/login`)
3. ThingsBoard devuelve un **token JWT de TB** + **customer_id** (si aplica)
4. FastAPI lo almacena en `_session_store` y emite su propio **JWT de sesion** (HS256, 24h, httpOnly cookie)
5. Requests subsiguientes: FastAPI extrae session token, busca el TB token asociado, filtra por `customer_id` si es `CUSTOMER_USER`

---

## API REST

| Endpoint | Descripcion |
|----------|-------------|
| `POST /api/login` | Login contra TB, devuelve JWT de sesion |
| `POST /api/logout` | Borra sesion y cookie |
| `GET /api/me` | Perfil del usuario logueado (authority, displayName, customerId) |
| `GET /api/devices` | Dispositivos del tenant/customer con atributos y telemetria |
| `GET /api/telemetry/{id}` | Ultimo valor de cada key (timeseries) |
| `GET /api/telemetry/{id}/history?keys=&startTs=&endTs=&agg=&interval=&limit=` | Historial |
| `GET /api/attributes/{id}` | Atributos del dispositivo |
| `GET /api/edges` | Edges del tenant/customer |
| `GET /api/monitoreo/history?deviceIds=&startTs=&endTs=` | Datos para pagina Monitoreo |
| `POST /api/rpc/valve` | Envia RPC `set_valve` al dispositivo |
| `POST /api/devices/create` | Crear nuevo dispositivo con atributos + auto-asignacion |
| `DELETE /api/devices/{id}` | Eliminar dispositivo |

---

## Paginas del Dashboard

### Dashboard (mapa + telemetria)
- Mapa Leaflet interactivo con Leaflet.markercluster
- Marcadores por tipo: verde (sensor activo), rojo (actuador), gris (inactivo)
- Conexiones visuales entre Gateway y sensores (lineas punteadas)
- Panel lateral con tarjetas de telemetria (variable principal + bateria + RSSI + uptime)
- Sparkline de 24h en miniatura
- Graficas en tiempo real con ventana deslizante de 1h

### Historicos
- Rango de fechas personalizado con inputs date
- Selector de intervalo de agregacion (desde 1 minuto hasta 24 horas, o sin agregacion)
- Grafico de linea + tarjetas de resumen (promedio, minimo, maximo, conteo)
- Tabla paginada (100 registros/pagina)
- Exportacion CSV
- Datos ordenados ascendentemente por timestamp

### Monitoreo
- Grafica unificada con 3 datasets sobrepuestos (temperatura, humedad, luz)
- Plugin de overlay de periodos de riego (rectangulos azules traslucidos)
- Plugin crosshair: linea vertical + puntos por dataset + tooltip personalizado
- Selector de periodo (1h, 6h, 12h, 24h)
- Leyenda con colores por variable

### Valvula
- Indicador de estado (abierta/cerrada) con animacion de pulso
- Botones tactiles ABRIR/CERRAR grandes
- Selector de dispositivo valvula
- Feedback toast en cada comando

### Agregar Nodo
- Formulario: nombre, tipo (sensor/actuador), tipo de sensor, lat/lng
- Minimapa Leaflet para seleccionar ubicacion
- Auto-asignacion a edge y customer (si aplica)
- Tarjeta de exito con credentials + instrucciones para el ESP32

---

## Tiempo real — WebSocket

### Cliente (realtime.js)

1. `startRealtimeCharts(deviceId)` — crea Chart.js con eje X lineal (`{x, y}` data format, `parsing`)
2. Abre `WebSocket` — el servidor sirve los datos del ultimo poll a TB
3. `onmessage`: usa `d._ts` del servidor como timestamp, agrega punto al chart
4. Guardia anti-race: descarta mensajes de deviceId anterior (`_currentDeviceId`)
5. `_slide()` timer cada 1s: actualiza `chart.options.scales.x.min/max` para ventana deslizante
6. `_trim()`: elimina puntos fuera de la ventana de 1h
7. Ventana fija de 1 hora (60 min) con auto-scroll

---

## Tema visual / Mobile

### Desktop (> 768px)
- Sidebar izquierdo (260px) con navegacion SPA, edge selector, theme toggle, logout
- Dashboard: mapa + panel derecho (panel-w 420px)
- Paginas internas centradas con max-width

### Mobile (< 768px)
- **Bottom tab bar**: 5 iconos (Inicio, Historia, Monitor, Riego, Agregar)
- **Top bar**: titulo "Granja" + theme toggle + hamburguesa que abre drawer lateral
- Sidebar entero como drawer con overlay oscuro (cierre con Escape/click fuera)
- Dashboard: mapa 40vh + panel abajo con scroll
- Historicos/Monitoreo: full-width, tabla con scroll horizontal
- Valvula: botones full-width
- Touch targets >= 44px

### Paleta
| Variable | Modo oscuro | Modo claro | Uso |
|----------|------------|-----------|-----|
| `--bg` | `#171410` | `#faf7f0` | Fondo principal |
| `--primary` | `#84cc16` | `#65a30d` | Verde lima (cultivo) |
| `--accent` | `#f59e0b` | `#f59e0b` | Ambar (trigo) |
| `--water` | `#06b6d4` | `#06b6d4` | Cyan (agua) |

---

## Multi-tenant (Customer Users)

El dashboard soporta usuarios tipo `CUSTOMER_USER`:

- Backend filtra `GET /api/devices` y `GET /api/edges` segun `customer_id`
- Customer users solo ven su edge asignado y sus dispositivos
- Edge selector se auto-selecciona y deshabilita para customers
- Al crear un nodo como customer, se asigna automaticamente al edge + customer

### Crear un customer con su usuario

```bash
cd granja-dashboard && python3 setup_customer.py
```

El script:
1. Autentica como tenant en TB Edge
2. Crea Customer "Finca" (si no existe)
3. Crea usuario `juan@finca.com` / `juan123` con authority `CUSTOMER_USER`
4. Activa el usuario y configura password
5. Asigna el edge "Granja-Raspberry" al customer
6. Asigna todos los dispositivos del edge al customer

---

## Variables de entorno

| Variable | Default | Descripcion |
|----------|---------|-------------|
| `TB_HOST` | `host.docker.internal` | Host de ThingsBoard Edge |
| `TB_PORT` | `8080` | Puerto de ThingsBoard Edge |
| `APP_PORT` | `3000` | Puerto de la app |
| `APP_SECRET` | `granja-dashboard-secret-key-change-in-production` | Clave para firmar JWT |

---

## Ejecucion

### Docker (produccion local)

```bash
cd otbr
./start.sh granja-dashboard   # build + run
# Abrir http://localhost:3000
```

### Desarrollo local

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
TB_HOST=localhost TB_PORT=8080 uvicorn app.main:app --host 0.0.0.0 --port 3000 --reload
```

### Acceso remoto

```bash
# Tailscale Funnel (HTTPS publico)
sudo tailscale serve --bg http://localhost:3000
sudo tailscale funnel --bg 3000
# URL: https://granja-iot.tailaf11de.ts.net
```

---

## Seguridad

- **Session tokens**: JWT HS256, 24h expiracion, cookies httpOnly + SameSite=Lax
- **TB tokens**: almacenados solo en memoria del servidor (nunca expuestos al cliente)
- **Sanitizacion HTML**: `App.esc()` escapa todo contenido dinamico
- **Customer isolation**: filtrado server-side por customer_id para CUSTOMER_USER
- **CORS**: no expuesto — el frontend y backend comparten origen
