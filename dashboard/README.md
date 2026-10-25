# Dashboard — Monitoreo y control

Aplicación web **FastAPI + SPA vanilla** para monitorear la finca y operar el riego sobre ThingsBoard. Interfaz *mobile-first* con mapa Leaflet, gráficas Chart.js en tiempo real, históricos, vista unificada de sensores y control de válvula por RPC.

| Dato | Valor |
|------|-------|
| Puerto | `3000` |
| Backend | FastAPI + httpx + Jinja2 |
| Frontend | JS vanilla + Leaflet 1.9 + Chart.js 4.4 |
| Autenticación | Sesión JWT propia sobre el login de ThingsBoard |
| Backend IoT | ThingsBoard Edge / CE vía REST |

---

## Índice

- [Arquitectura](#arquitectura)
- [Estructura](#estructura)
- [Ejecución](#ejecución)
- [Autenticación](#autenticación)
- [API REST](#api-rest)
- [Páginas](#páginas)
- [Tiempo real por WebSocket](#tiempo-real-por-websocket)
- [Multi-tenant](#multi-tenant)
- [Variables de entorno](#variables-de-entorno)
- [Diseño y responsive](#diseño-y-responsive)
- [Seguridad](#seguridad)

---

## Arquitectura

```
┌──────────────┐   HTTP    ┌──────────────────┐   REST    ┌──────────────┐
│  Navegador   │ ◄───────► │ FastAPI (:3000)  │ ◄───────► │ ThingsBoard  │
│  SPA vanilla │           │ backend + WS     │           │  (:8080)     │
└──────────────┘           └────────┬─────────┘           └──────────────┘
                                    │
                              ┌─────▼─────┐
                              │  Sesiones │  token TB en memoria del servidor
                              └───────────┘
```

El backend actúa como **proxy autenticado**: el token de ThingsBoard nunca llega al navegador, y el filtrado por `customer_id` se hace en el servidor.

---

## Estructura

```
dashboard/
├── Dockerfile
├── docker-compose.yml
├── requirements.txt
├── scripts/
│   ├── compose-up.sh        # build + run del stack
│   └── dev.sh               # uvicorn local sin Docker (--reload opcional)
├── tools/
│   └── create_customer.py   # Crea customer + usuario + asignaciones en ThingsBoard
└── app/
    ├── config.py            # Variables de entorno (TB_HOST, TB_PORT, APP_SECRET)
    ├── thingsboard.py       # Cliente HTTP asíncrono para la REST API de ThingsBoard
    ├── auth.py              # Emisión y validación del JWT de sesión
    ├── main.py              # Rutas, WebSocket y endpoints
    ├── templates/
    │   ├── base.html        # Shell: sidebar, bottom nav, top bar
    │   ├── login.html       # Página de login
    │   └── dashboard.html   # Mapa + panel de telemetría
    └── static/
        ├── css/style.css    # Tema agrícola claro/oscuro, mobile-first
        └── js/
            ├── app.js       # Estado, router SPA, cliente HTTP, toasts
            ├── map.js       # Marcadores, gateway, enlaces y uptime
            ├── realtime.js  # Gráficas en vivo, ventana deslizante, WebSocket
            ├── historicos.js# Consultas históricas, gráfica, tabla y CSV
            ├── monitoreo.js # Tres sensores + periodos de riego + crosshair
            └── valve.js     # Control de válvula por RPC
```

---

## Ejecución

### Docker

```bash
cd dashboard
./scripts/compose-up.sh              # todos los servicios
./scripts/compose-up.sh dashboard    # solo el dashboard
```

Abrir `http://localhost:3000`.

> El script siempre construye con `--build`; una imagen cacheada puede servir código viejo.

### Local

```bash
cd dashboard
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
./scripts/dev.sh
```

Requiere ThingsBoard accesible en `TB_HOST:TB_PORT` (por defecto `localhost:8080`).

### Crear un customer con su usuario

```bash
python3 tools/create_customer.py
```

El script autentica como tenant, crea el customer `Finca`, el usuario `juan@finca.com`, asigna el edge `Granja-Raspberry` y todos sus dispositivos, y activa la cuenta.

---

## Autenticación

1. El usuario envía sus credenciales a `POST /api/login`.
2. FastAPI autentica contra ThingsBoard (`POST /api/auth/login`).
3. ThingsBoard devuelve el token y el `customer_id` (si es `CUSTOMER_USER`).
4. FastAPI guarda el token en memoria (`_session_store`) y emite su propio JWT HS256 en una cookie `httpOnly` de 24 h.
5. Cada petición posterior resuelve el token de sesión, recupera el token de ThingsBoard y filtra por `customer_id`.

---

## API REST

| Endpoint | Descripción |
|----------|-------------|
| `POST /api/login` | Login contra ThingsBoard; emite el JWT de sesión |
| `POST /api/logout` | Borra la sesión y la cookie |
| `GET /api/me` | Perfil del usuario (authority, nombre, customerId) |
| `GET /api/devices` | Dispositivos del tenant/customer con atributos y última telemetría |
| `GET /api/telemetry/{id}` | Último valor de cada clave |
| `GET /api/telemetry/{id}/history` | Histórico con `keys`, `startTs`, `endTs`, `agg`, `interval`, `limit` |
| `GET /api/attributes/{id}` | Atributos del dispositivo |
| `GET /api/edges` | Edges visibles para el usuario |
| `GET /api/monitoreo/history` | Datos combinados para la página Monitoreo |
| `POST /api/rpc/valve` | Envía el RPC `set_valve` al actuador |
| `POST /api/devices/create` | Crea un dispositivo con atributos y asignación automática |
| `DELETE /api/devices/{id}` | Elimina un dispositivo |
| `WS /ws/{device_id}` | Telemetría en vivo |

---

## Páginas

### Dashboard
Mapa Leaflet con *markercluster* y conexiones hacia el gateway, panel de tarjetas por nodo (variable principal, RSSI, uptime), sparkline de 24 h y gráficas en vivo con ventana deslizante de 1 h.

### Históricos
Rango de fechas personalizado, intervalo de agregación (1 min a 24 h o sin agregar), gráfica de línea, tarjetas de resumen (promedio, mínimo, máximo, conteo), tabla paginada de 100 registros y exportación CSV.

### Monitoreo
Gráfica única con temperatura, humedad y luz superpuestas, overlay azul de los periodos de riego, crosshair con tooltip por dataset y selector de periodo (1 h, 6 h, 12 h, 24 h).

### Válvula
Estado abierta/cerrada con animación, botones táctiles de apertura y cierre, selector de dispositivo y confirmación por toast.

### Agregar Nodo
Formulario con nombre, tipo de sensor, posición elegida en un minimapa y asignación automática a edge y customer. Al finalizar muestra las credenciales y los pasos para flashear el ESP32.

---

## Tiempo real por WebSocket

1. `startRealtimeCharts(deviceId)` crea la gráfica con eje X lineal.
2. El servidor hace polling a ThingsBoard y empuja cada muestra por `WS /ws/{device_id}`.
3. El cliente usa el `ts` del servidor y descarta mensajes de un `device_id` anterior (guardia anti-carrera).
4. Un temporizador de 1 s desplaza la ventana (`x.min` / `x.max`) y elimina los puntos fuera de la hora actual.

---

## Multi-tenant

Los usuarios `CUSTOMER_USER` solo ven su edge y sus dispositivos:

- `GET /api/devices` y `GET /api/edges` filtran por `customer_id`.
- El selector de edge se autoselecciona y queda bloqueado.
- Un nodo creado por un customer se asigna automáticamente a su edge y customer.

---

## Variables de entorno

| Variable | Default | Descripción |
|----------|---------|-------------|
| `TB_HOST` | `host.docker.internal` | Host de ThingsBoard |
| `TB_PORT` | `8080` | Puerto de ThingsBoard |
| `APP_SECRET` | valor de laboratorio | Clave de firma del JWT de sesión |

`APP_SECRET` debe definirse explícitamente fuera de un entorno de laboratorio.

---

## Diseño y responsive

| Breakpoint | Comportamiento |
|------------|----------------|
| > 768 px | Sidebar de 260 px, mapa + panel lateral de 420 px |
| < 768 px | Bottom tab bar de 5 accesos, top bar con drawer, mapa 40 vh, objetivos táctiles ≥ 44 px |

Tema claro y oscuro persistidos en `localStorage`.

| Token | Oscuro | Claro | Uso |
|-------|--------|-------|-----|
| `--bg` | `#171410` | `#faf7f0` | Fondo |
| `--primary` | `#84cc16` | `#65a30d` | Verde cultivo |
| `--accent` | `#f59e0b` | `#f59e0b` | Ámbar |
| `--water` | `#06b6d4` | `#06b6d4` | Agua / riego |

---

## Seguridad

- Sesiones JWT HS256 con 24 h de expiración, cookie `httpOnly` y `SameSite=Lax`.
- El token de ThingsBoard se mantiene solo en memoria del servidor.
- Todo contenido dinámico se escapa con `App.esc()` antes de insertarse en el DOM.
- El aislamiento por customer se aplica del lado del servidor.
- Frontend y backend comparten origen: no se expone CORS.
