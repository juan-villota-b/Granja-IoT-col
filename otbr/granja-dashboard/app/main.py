import json
import asyncio
from datetime import datetime, timezone
from typing import Optional

from fastapi import FastAPI, Request, HTTPException, WebSocket, WebSocketDisconnect, Depends, Query
from fastapi.responses import HTMLResponse, RedirectResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
import httpx

from app.config import TB_BASE_URL, APP_PORT
from app.tb_client import TBClient
from app.auth import create_session_token, get_current_user

app = FastAPI(title="Granja Dashboard")

app.mount("/static", StaticFiles(directory="app/static"), name="static")
templates = Jinja2Templates(directory="app/templates")

tb = TBClient()

# ─── Session store: token -> tb_token ──────────────────────────────
_session_store: dict[str, str] = {}
_store_lock = asyncio.Lock()


async def store_tb_token(session_token: str, tb_token: str):
    async with _store_lock:
        _session_store[session_token] = tb_token


async def get_tb_token(session_token: str) -> Optional[str]:
    async with _store_lock:
        return _session_store.get(session_token)


# ─── Pages ──────────────────────────────────────────────────────────

@app.get("/", response_class=HTMLResponse)
async def index(request: Request):
    token = request.cookies.get("session_token")
    if token and await get_tb_token(token):
        return templates.TemplateResponse("dashboard.html", {"request": request})
    return templates.TemplateResponse("login.html", {"request": request})


@app.get("/login", response_class=HTMLResponse)
async def login_page(request: Request):
    return templates.TemplateResponse("login.html", {"request": request})


@app.get("/dashboard", response_class=HTMLResponse)
async def dashboard_page(request: Request):
    return templates.TemplateResponse("dashboard.html", {"request": request})


# ─── Auth API ───────────────────────────────────────────────────────

@app.post("/api/login")
async def api_login(request: Request):
    body = await request.json()
    username = body.get("username")
    password = body.get("password")
    if not username or not password:
        raise HTTPException(status_code=400, detail="username y password requeridos")

    tb_token = await tb.login(username, password)
    if not tb_token:
        raise HTTPException(status_code=401, detail="Credenciales inválidas")

    session_token = create_session_token(username)
    await store_tb_token(session_token, tb_token)

    response = JSONResponse({"ok": True, "username": username})
    response.set_cookie(
        key="session_token",
        value=session_token,
        httponly=True,
        max_age=86400,
        samesite="lax",
    )
    return response


@app.post("/api/logout")
async def api_logout(request: Request):
    token = request.cookies.get("session_token")
    if token:
        async with _store_lock:
            _session_store.pop(token, None)
    response = JSONResponse({"ok": True})
    response.delete_cookie("session_token")
    return response


# ─── Devices API ────────────────────────────────────────────────────

@app.get("/api/devices")
async def api_devices(request: Request, gateway: str = "IoT-Gateway"):
    session_token = request.cookies.get("session_token")
    if not session_token:
        raise HTTPException(status_code=401, detail="No autenticado")
    tb_token = await get_tb_token(session_token)
    if not tb_token:
        raise HTTPException(status_code=401, detail="Sesión expirada")

    all_devices = await tb.get_tenant_devices(tb_token)

    filtered = []
    for dev in all_devices:
        name = dev.get("name", "")
        dtype = dev.get("type", "")
        if name != gateway and dtype != "sensor_sed":
            continue
        dev_id = dev.get("id", {}).get("id")
        if not dev_id:
            continue
        filtered.append((dev_id, name, dtype, dev.get("type", "default"), dev.get("label", "")))

    # Fetch attributes and telemetry in parallel for all devices
    tasks = []
    for dev_id, _, _, _, _ in filtered:
        tasks.append(tb.get_device_attributes(tb_token, dev_id))
        tasks.append(tb.get_latest_telemetry(tb_token, dev_id))
    results = await asyncio.gather(*tasks, return_exceptions=True)

    result = []
    for i, (dev_id, name, dtype, devtype, label) in enumerate(filtered):
        attrs = results[i * 2]
        telemetry = results[i * 2 + 1]
        if isinstance(attrs, Exception):
            attrs = {}
        if isinstance(telemetry, Exception):
            telemetry = {}
        result.append({
            "id": dev_id,
            "name": name,
            "type": devtype,
            "label": label,
            "attributes": attrs,
            "telemetry": telemetry,
        })

    return {"devices": result}


@app.get("/api/telemetry/{device_id}")
async def api_telemetry(device_id: str, request: Request, keys: str = ""):
    session_token = request.cookies.get("session_token")
    if not session_token:
        raise HTTPException(status_code=401, detail="No autenticado")
    tb_token = await get_tb_token(session_token)
    if not tb_token:
        raise HTTPException(status_code=401, detail="Sesión expirada")

    data = await tb.get_latest_telemetry(tb_token, device_id, keys)
    return data


@app.get("/api/telemetry/{device_id}/history")
async def api_telemetry_history(
    device_id: str,
    request: Request,
    keys: str = Query(...),
    startTs: int = Query(..., alias="startTs"),
    endTs: int = Query(..., alias="endTs"),
    agg: str = Query("AVG"),
    interval: int = Query(3600000),
):
    session_token = request.cookies.get("session_token")
    if not session_token:
        raise HTTPException(status_code=401, detail="No autenticado")
    tb_token = await get_tb_token(session_token)
    if not tb_token:
        raise HTTPException(status_code=401, detail="Sesión expirada")

    data = await tb.get_telemetry_history(
        tb_token, device_id, keys, startTs, endTs, agg, interval
    )
    return data


@app.get("/api/attributes/{device_id}")
async def api_attributes(device_id: str, request: Request):
    session_token = request.cookies.get("session_token")
    if not session_token:
        raise HTTPException(status_code=401, detail="No autenticado")
    tb_token = await get_tb_token(session_token)
    if not tb_token:
        raise HTTPException(status_code=401, detail="Sesión expirada")

    attrs = await tb.get_device_attributes(tb_token, device_id)
    return attrs


@app.post("/api/rpc/valve")
async def api_valve_rpc(request: Request):
    session_token = request.cookies.get("session_token")
    if not session_token:
        raise HTTPException(status_code=401, detail="No autenticado")
    tb_token = await get_tb_token(session_token)
    if not tb_token:
        raise HTTPException(status_code=401, detail="Sesión expirada")

    body = await request.json()
    device_id = body.get("device_id")
    state = body.get("state")
    if not device_id or state not in (0, 1):
        raise HTTPException(status_code=400, detail="device_id y state (0|1) requeridos")

    ok = await tb.send_rpc(tb_token, device_id, "set_valve", {"state": state})
    return {"ok": ok}


# ─── WebSocket for real-time telemetry ──────────────────────────────

connected_websockets: dict[str, list[WebSocket]] = {}


@app.websocket("/ws/{device_id}")
async def websocket_endpoint(websocket: WebSocket, device_id: str):
    await websocket.accept()
    if device_id not in connected_websockets:
        connected_websockets[device_id] = []
    connected_websockets[device_id].append(websocket)

    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        connected_websockets[device_id].remove(websocket)


async def poll_and_broadcast():
    while True:
        async with _store_lock:
            sessions = list(_session_store.items())
        for session_token, tb_token in sessions:
            try:
                devices = await tb.get_tenant_devices(tb_token)
                for dev in devices:
                    dev_id = dev.get("id", {}).get("id")
                    if not dev_id or dev_id not in connected_websockets:
                        continue
                    telemetry = await tb.get_latest_telemetry(tb_token, dev_id, "temperature,humidity,battery,rssi,uptime")
                    if not telemetry:
                        continue
                    attrs = await tb.get_device_attributes(tb_token, dev_id)
                    active_val = attrs.get("active")
                    last_activity = attrs.get("lastActivityTime")
                    extras = {}
                    if active_val is not None:
                        extras["_active"] = bool(active_val) if not isinstance(active_val, bool) else active_val
                    if last_activity is not None:
                        extras["_lastActivityTime"] = last_activity
                    msg = json.dumps({"device_id": dev_id, **telemetry, **extras})
                    for ws in connected_websockets[dev_id][:]:
                        try:
                            await ws.send_text(msg)
                        except Exception:
                            connected_websockets[dev_id].remove(ws)
            except Exception as e:
                import logging
                logging.getLogger("dashboard").warning("poll error: %s", e)
        await asyncio.sleep(5)


@app.on_event("startup")
async def startup():
    asyncio.create_task(poll_and_broadcast())
