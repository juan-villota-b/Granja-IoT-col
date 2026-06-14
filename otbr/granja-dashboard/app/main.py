import json
import asyncio
import os
import secrets
import string
import logging

from fastapi import FastAPI, Request, HTTPException, WebSocket, WebSocketDisconnect, Query
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from app.tb_client import TBClient
from app.auth import create_session_token, require_session

logger = logging.getLogger("dashboard")

_PROV_ALPHABET = string.ascii_uppercase + string.digits

app = FastAPI(title="Granja Dashboard")

app.mount("/static", StaticFiles(directory="app/static"), name="static")
templates = Jinja2Templates(directory="app/templates")

tb = TBClient()

_session_store: dict[str, dict] = {}
_store_lock = asyncio.Lock()


async def store_session(session_token: str, data: dict):
    async with _store_lock:
        _session_store[session_token] = data


async def get_session(session_token: str) -> dict | None:
    async with _store_lock:
        return _session_store.get(session_token)


# ═══════════════════════════════════════════════════════════════════
#  Pages
# ═══════════════════════════════════════════════════════════════════

@app.get("/", response_class=HTMLResponse)
async def index(request: Request):
    token = request.cookies.get("session_token")
    if token and await get_session(token):
        return templates.TemplateResponse("dashboard.html", {"request": request})
    return templates.TemplateResponse("login.html", {"request": request})


@app.get("/login", response_class=HTMLResponse)
async def login_page(request: Request):
    return templates.TemplateResponse("login.html", {"request": request})


@app.get("/dashboard", response_class=HTMLResponse)
async def dashboard_page(request: Request):
    return templates.TemplateResponse("dashboard.html", {"request": request})


# ═══════════════════════════════════════════════════════════════════
#  Auth API
# ═══════════════════════════════════════════════════════════════════

@app.post("/api/login")
async def api_login(request: Request):
    body = await request.json()
    username = body.get("username")
    password = body.get("password")
    if not username or not password:
        raise HTTPException(status_code=400, detail="username y password requeridos")

    tb_token = await tb.login(username, password)
    if not tb_token:
        raise HTTPException(status_code=401, detail="Credenciales invalidas")

    user_info = await tb.get_current_user(tb_token)
    session_token = create_session_token(username)
    await store_session(session_token, {
        "tb_token": tb_token,
        "username": username,
        "authority": user_info["authority"],
        "customer_id": user_info["customer_id"],
        "first_name": user_info["first_name"],
    })

    response = JSONResponse({
        "ok": True,
        "username": username,
        "display_name": user_info["first_name"] or username,
        "authority": user_info["authority"],
    })
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


@app.get("/api/me")
async def api_me(request: Request):
    await require_session(request)
    token = request.cookies.get("session_token")
    session = await get_session(token)
    if not session:
        raise HTTPException(status_code=401, detail="Sesion expirada")
    first = session.get("first_name", "")
    return {
        "username": session["username"],
        "display_name": first if first else session["username"],
        "authority": session["authority"],
    }


# ═══════════════════════════════════════════════════════════════════
#  Edges API
# ═══════════════════════════════════════════════════════════════════

@app.get("/api/edges")
async def api_edges(request: Request):
    await require_session(request)
    token = request.cookies.get("session_token")
    session = await get_session(token)
    if not session:
        raise HTTPException(status_code=401, detail="Sesion expirada")
    tb_token = session["tb_token"]

    if session["authority"] == "CUSTOMER_USER" and session.get("customer_id"):
        edges = await tb.get_customer_edges(tb_token, session["customer_id"])
    else:
        edges = await tb.get_tenant_edges(tb_token)

    result = []
    for e in edges:
        eid = e.get("id", {}).get("id")
        if not eid:
            continue
        result.append({
            "id": eid,
            "name": e.get("name", ""),
        })
    return {"edges": result}


# ═══════════════════════════════════════════════════════════════════
#  Devices API
# ═══════════════════════════════════════════════════════════════════

@app.get("/api/devices")
async def api_devices(request: Request):
    await require_session(request)
    token = request.cookies.get("session_token")
    session = await get_session(token)
    if not session:
        raise HTTPException(status_code=401, detail="Sesion expirada")
    tb_token = session["tb_token"]

    if session["authority"] == "CUSTOMER_USER" and session.get("customer_id"):
        all_devices = await tb.get_customer_devices(tb_token, session["customer_id"])
    else:
        all_devices = await tb.get_tenant_devices(tb_token)

    filtered = []
    for dev in all_devices:
        name = dev.get("name", "")
        did = dev.get("id", {}).get("id")
        dtype = dev.get("type", "")
        if not did:
            continue
        filtered.append((did, name, dtype, dev.get("type", "default"), dev.get("label", "")))

    tasks = []
    for did, _, _, _, _ in filtered:
        tasks.append(tb.get_device_attributes(tb_token, did))
        tasks.append(tb.get_latest_telemetry(tb_token, did))
    results = await asyncio.gather(*tasks, return_exceptions=True)

    result = []
    for i, (did, name, dtype, devtype, label) in enumerate(filtered):
        attrs = results[i * 2]
        telemetry = results[i * 2 + 1]
        if isinstance(attrs, Exception):
            attrs = {}
        if isinstance(telemetry, Exception):
            telemetry = {}
        if not attrs.get("sensor_type"):
            for sk in ("temperature", "humidity", "light", "valve", "battery"):
                v = telemetry.get(sk)
                if v is not None:
                    attrs["sensor_type"] = sk
                    break
        result.append({
            "id": did,
            "name": name,
            "type": devtype,
            "label": label,
            "attributes": attrs,
            "telemetry": telemetry,
        })

    return {"devices": result}


@app.delete("/api/devices/{device_id}")
async def api_devices_delete(device_id: str, request: Request):
    await require_session(request)
    token = request.cookies.get("session_token")
    session = await get_session(token)
    if not session:
        raise HTTPException(status_code=401, detail="Sesion expirada")

    ok = await tb.delete_device(session["tb_token"], device_id)
    if not ok:
        raise HTTPException(status_code=500, detail="No se pudo eliminar el dispositivo")
    return {"ok": True}


@app.get("/api/telemetry/{device_id}")
async def api_telemetry(device_id: str, request: Request, keys: str = ""):
    await require_session(request)
    token = request.cookies.get("session_token")
    session = await get_session(token)
    if not session:
        raise HTTPException(status_code=401, detail="Sesion expirada")

    data = await tb.get_latest_telemetry(session["tb_token"], device_id, keys)
    return data


@app.get("/api/telemetry/{device_id}/history")
async def api_telemetry_history(
    device_id: str,
    request: Request,
    keys: str = Query(...),
    startTs: int = Query(..., alias="startTs"),
    endTs: int = Query(..., alias="endTs"),
    agg: str = Query(""),
    interval: int = Query(0),
    limit: int = Query(1000),
):
    await require_session(request)
    token = request.cookies.get("session_token")
    session = await get_session(token)
    if not session:
        raise HTTPException(status_code=401, detail="Sesion expirada")

    data = await tb.get_telemetry_history(
        session["tb_token"], device_id, keys, startTs, endTs, agg, interval, limit
    )
    return data


@app.get("/api/monitoreo/history")
async def api_monitoreo_history(
    request: Request,
    startTs: int = Query(..., alias="startTs"),
    endTs: int = Query(..., alias="endTs"),
    agg: str = Query(""),
    interval: int = Query(0),
    limit: int = Query(2000),
):
    await require_session(request)
    token = request.cookies.get("session_token")
    session = await get_session(token)
    if not session:
        raise HTTPException(status_code=401, detail="Sesion expirada")
    tb_token = session["tb_token"]

    if session["authority"] == "CUSTOMER_USER" and session.get("customer_id"):
        all_devices = await tb.get_customer_devices(tb_token, session["customer_id"])
    else:
        all_devices = await tb.get_tenant_devices(tb_token)

    sensor_devices = {}
    valve_device_id = None
    for dev in all_devices:
        did = dev.get("id", {}).get("id")
        if not did:
            continue
        attrs = await tb.get_device_attributes(tb_token, did)
        st = attrs.get("sensor_type", "")
        if st in ("temperature", "humidity", "light"):
            sensor_devices[st] = did
        elif st == "valve":
            valve_device_id = did
        if len(sensor_devices) >= 3 and valve_device_id:
            break

    result = {"sensors": {}, "valve": []}

    for st, did in sensor_devices.items():
        try:
            data = await tb.get_telemetry_history(tb_token, did, st, startTs, endTs, agg, interval, limit)
            if st in data:
                result["sensors"][st] = data[st]
        except Exception:
            pass

    if valve_device_id:
        try:
            vdata = await tb.get_telemetry_history(tb_token, valve_device_id, "valve", startTs, endTs, agg, interval, limit)
            if "valve" in vdata:
                result["valve"] = vdata["valve"]
        except Exception:
            pass

    return result


@app.get("/api/attributes/{device_id}")
async def api_attributes(device_id: str, request: Request):
    await require_session(request)
    token = request.cookies.get("session_token")
    session = await get_session(token)
    if not session:
        raise HTTPException(status_code=401, detail="Sesion expirada")

    attrs = await tb.get_device_attributes(session["tb_token"], device_id)
    return attrs


@app.post("/api/devices/create")
async def api_devices_create(request: Request):
    await require_session(request)
    token = request.cookies.get("session_token")
    session = await get_session(token)
    if not session:
        raise HTTPException(status_code=401, detail="Sesion expirada")
    tb_token = session["tb_token"]

    body = await request.json()
    name = body.get("name", "").strip()
    zone = body.get("zone", "").strip()
    sensor_type = body.get("sensor_type", "temperature")
    lat = float(body.get("lat", 0))
    lng = float(body.get("lng", 0))
    edge_id = body.get("edge_id", "")

    if not name:
        raise HTTPException(status_code=400, detail="name es requerido")
    if sensor_type not in ("temperature", "humidity", "light", "valve"):
        raise HTTPException(status_code=400, detail="sensor_type debe ser temperature, humidity, light o valve")

    created = await tb.create_device(tb_token, name, "sensor_sed", name)
    device_id = created.get("id", {}).get("id")
    if not device_id:
        raise HTTPException(status_code=500, detail="No se pudo crear el dispositivo en TB CE")

    access_token = await tb.get_device_credentials(tb_token, device_id)

    device_type_label = {"temperature": "th_auto", "humidity": "th_auto", "light": "light_sed", "valve": "actuator_pump"}
    server_attrs = {
        "zone": zone,
        "lat": lat,
        "lng": lng,
        "provisioning_key": access_token,
        "sensor_type": sensor_type,
        "status": "pending",
        "type": device_type_label.get(sensor_type, "sensor_sed"),
    }
    await tb.set_server_attributes(tb_token, device_id, server_attrs)

    if edge_id:
        await tb.assign_device_to_edge(tb_token, edge_id, device_id)

    customer_id = session.get("customer_id")
    if customer_id and session.get("authority") == "CUSTOMER_USER":
        await tb.assign_device_to_customer(tb_token, customer_id, device_id)

    return {
        "ok": True,
        "device_id": device_id,
        "device_name": name,
        "access_token": access_token,
        "sensor_type": sensor_type,
        "zone": zone,
        "lat": lat,
        "lng": lng,
    }


@app.post("/api/rpc/valve")
async def api_valve_rpc(request: Request):
    await require_session(request)
    token = request.cookies.get("session_token")
    session = await get_session(token)
    if not session:
        raise HTTPException(status_code=401, detail="Sesion expirada")

    body = await request.json()
    device_id = body.get("device_id")
    state = body.get("state")
    if not device_id or state not in (0, 1):
        raise HTTPException(status_code=400, detail="device_id y state (0|1) requeridos")

    # Send RPC to TB Edge (bridge MQTT gateway is connected there)
    import httpx
    tb_edge_url = os.getenv("TB_EDGE_URL", "http://192.168.1.114:8082")
    async with httpx.AsyncClient(timeout=10.0) as edge_client:
        login_resp = await edge_client.post(
            f"{tb_edge_url}/api/auth/login",
            json={"username": "tenant@thingsboard.org", "password": "tenant"},
        )
        if login_resp.status_code != 200:
            return {"ok": False, "error": "No se pudo autenticar en TB Edge"}
        edge_token = login_resp.json().get("token")
        rpc_resp = await edge_client.post(
            f"{tb_edge_url}/api/plugins/rpc/oneway/{device_id}",
            json={"method": "set_valve", "params": {"state": state}},
            headers={"X-Authorization": f"Bearer {edge_token}"},
        )
        ok = rpc_resp.status_code == 200

    return {"ok": ok}


# ═══════════════════════════════════════════════════════════════════
#  WebSocket for real-time telemetry
# ═══════════════════════════════════════════════════════════════════

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
            sessions = list(_session_store.values())

        active_ids = [did for did, ws_list in connected_websockets.items() if ws_list]

        if active_ids:
            for session in sessions:
                tb_token = session.get("tb_token")
                if not tb_token:
                    continue
                try:
                    for did in active_ids:
                        if did not in connected_websockets or not connected_websockets[did]:
                            continue
                        telemetry = await tb.get_latest_telemetry(
                            tb_token, did,
                            "temperature,humidity,light,valve,battery,rssi,uptime"
                        )
                        if not telemetry:
                            continue
                        attrs = await tb.get_device_attributes(tb_token, did)
                        active_val = attrs.get("active")
                        last_activity = attrs.get("lastActivityTime")
                        extras = {}
                        if active_val is not None:
                            extras["_active"] = bool(active_val) if not isinstance(active_val, bool) else active_val
                        if last_activity is not None:
                            extras["_lastActivityTime"] = last_activity
                        msg = json.dumps({"device_id": did, **telemetry, **extras})
                        for ws in connected_websockets[did][:]:
                            try:
                                await ws.send_text(msg)
                            except Exception:
                                connected_websockets[did].remove(ws)
                except Exception as e:
                    logger.warning("poll error: %s", e)

        await asyncio.sleep(5)


@app.on_event("startup")
async def startup():
    asyncio.create_task(poll_and_broadcast())
