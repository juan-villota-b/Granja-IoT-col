#!/usr/bin/env python3

import argparse, asyncio, json, logging, os, time, yaml, cbor2, httpx

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s"
)
log = logging.getLogger("bridge")

import aiocoap
from aiocoap import resource, CONTENT, BAD_REQUEST

from mqtt.publisher  import MQTTPublisher
from mqtt.subscriber import MQTTSubscriber
from automation.irrigation import IrrigationController

NODES_FILE = "/data/registered_nodes.json"


# ═══════════════════════════════════════════════════════════════════
#  CoAP Resource /readings
# ═══════════════════════════════════════════════════════════════════

class ReadingsResource(resource.Resource):
    def __init__(self, bridge):
        super().__init__()
        self._bridge = bridge

    async def render_post(self, request):
        src = request.remote.hostinfo if request.remote else None
        ipv6 = str(src) if src else "unknown"
        if ']' in ipv6: ipv6 = ipv6.split(']')[0]
        if '[' in ipv6: ipv6 = ipv6.split('[')[-1]
        if '%' in ipv6: ipv6 = ipv6.split('%')[0]

        try:
            data = cbor2.loads(request.payload)
        except Exception:
            return aiocoap.Message(code=BAD_REQUEST)

        now_ms = int(time.time() * 1000)

        # ── Provisioning vs regular telemetry ──
        prov_key = data.get("prov_key", "")
        state = data.get("state", "active")

        if prov_key:
            nid = await self._bridge.resolve_device_name(prov_key)
            if not nid:
                log.warning("PROV-KEY UNKNOWN: %s from %s", prov_key, ipv6)
                resp_payload = cbor2.dumps({"cmd": "wait", "msg": "Device not provisioned yet"})
                return aiocoap.Message(code=CONTENT, payload=resp_payload, content_format=60)
        else:
            nid = data.get("id", "")
            if not nid:
                return aiocoap.Message(code=BAD_REQUEST)

        # ── Auto-registro MQTT ──
        is_new = nid not in self._bridge.nodes
        if is_new:
            ntype = data.get("tp") or self._bridge._dev_types_cache.get(nid, "sensor_sed")
            lat = data.get("lat") or self._bridge._dev_pos_cache.get(nid, (0.0, 0.0))[0]
            lng = data.get("lng") or self._bridge._dev_pos_cache.get(nid, (0.0, 0.0))[1]
            zone = data.get("zn", "")
            self._bridge.nodes[nid] = {
                "id": nid, "ipv6": ipv6,
                "zone": zone, "type": ntype,
                "lat": lat, "lng": lng,
            }
            if prov_key:
                self._bridge.nodes[nid]["prov_key"] = prov_key
            self._bridge._save_nodes()
            self._bridge.mqtt.connect_dev(nid, ntype)
            self._bridge.mqtt.attributes(nid, {
                "zone": zone,
                "type": ntype,
                "lat": round(lat, 6),
                "lng": round(lng, 6),
                "provisioning_key": prov_key,
                "status": "active",
            })
            log.info("AUTO-REGISTER %s from %s (prov_key=%s)", nid, ipv6, prov_key)

        # ── Si el ESP32 manda state=pending, solo queremos reclamar ──
        if state == "pending" and is_new:
            log.info("CLAIM %s — enviando comando start", nid)
            resp_payload = cbor2.dumps({"cmd": "start", "name": nid})
            return aiocoap.Message(code=CONTENT, payload=resp_payload, content_format=60)

        # ── Telemetria ──
        tb_telemetry = {}
        log_parts = []

        if "t" in data:
            tb_telemetry["temperature"] = round(data["t"], 1)
            log_parts.append(f"t={tb_telemetry['temperature']}°C")
        if "h" in data:
            tb_telemetry["humidity"] = int(data["h"])
            log_parts.append(f"h={tb_telemetry['humidity']}%")
        if "l" in data:
            tb_telemetry["light"] = int(data["l"])
            log_parts.append(f"l={tb_telemetry['light']}%")
        if "v" in data:
            tb_telemetry["valve"] = int(data["v"])
            log_parts.append(f"valve={tb_telemetry['valve']}")
        if "b" in data:
            tb_telemetry["battery"] = int(data["b"])
            log_parts.append(f"bat={tb_telemetry['battery']}mV")
        if "r" in data:
            tb_telemetry["rssi"] = int(data["r"])
        if "u" in data:
            tb_telemetry["uptime"] = int(data["u"])

        if tb_telemetry:
            self._bridge.mqtt.telemetry(nid, now_ms, tb_telemetry)
            self._bridge.irrigation.feed(nid, tb_telemetry)
            log.info(">>> %s %s rssi=%d up=%ds",
                     nid,
                     " ".join(log_parts) if log_parts else "(sin dato)",
                     tb_telemetry.get("rssi", 0),
                     tb_telemetry.get("uptime", 0))

        # ── Downlink: comandos pendientes ──
        cmd = self._bridge.pending_commands.pop(nid, None)
        resp = b'\xa0'
        if cmd:
            log.info("DOWNLINK %s -> %s", nid, cmd)
            try:
                resp = cbor2.dumps(cmd)
            except Exception:
                resp = b'\xa0'

        return aiocoap.Message(code=CONTENT, payload=resp, content_format=60)


# ═══════════════════════════════════════════════════════════════════
#  Bridge
# ═══════════════════════════════════════════════════════════════════

class Bridge:
    def __init__(self, path):
        with open(path) as f:
            self.cfg = yaml.safe_load(f)
        self.mqtt = MQTTPublisher(self.cfg)
        self.rpc = None  # merged into mqtt publisher
        self.nodes = {}
        self.pending_commands = {}
        self._prov_cache = {}       # prov_key → device_name
        self._dev_types_cache = {}  # device_name → type
        self._dev_pos_cache = {}    # device_name → (lat, lng)
        self._run = True

        self.irrigation = IrrigationController(
            actuator_nid=self.cfg.get("actuator_nid", "BOMBA"),
            bridge=self,
            thresholds=self.cfg.get("irrigation", {}),
        )

        # TB Edge HTTP config
        tb_cfg = self.cfg.get("tb_edge", {})
        self._tb_url = tb_cfg.get("url", "http://localhost:8080")
        self._tb_user = tb_cfg.get("username", "tenant@thingsboard.org")
        self._tb_pass = tb_cfg.get("password", "tenant")
        self._tb_token = None
        self._http = httpx.AsyncClient(timeout=15.0)

        self._load_nodes()

    # ── Persistencia ──────────────────────────────────────────────

    def _load_nodes(self):
        if not os.path.exists(NODES_FILE):
            log.info("No registered nodes file found, starting fresh")
            return
        try:
            with open(NODES_FILE) as f:
                self.nodes = json.load(f)
            log.info("Loaded %d previously registered nodes", len(self.nodes))
            for nid, node in self.nodes.items():
                pk = node.get("prov_key", "")
                if pk:
                    self._prov_cache[pk] = nid
                    self._dev_types_cache[nid] = node.get("type", "sensor_sed")
                    self._dev_pos_cache[nid] = (node.get("lat", 0.0), node.get("lng", 0.0))
                log.info("  -> %s", nid)
        except Exception as e:
            log.warning("Could not load nodes file: %s", e)

    def _save_nodes(self):
        try:
            os.makedirs(os.path.dirname(NODES_FILE), exist_ok=True)
            with open(NODES_FILE, "w") as f:
                json.dump(self.nodes, f, indent=2)
        except Exception as e:
            log.warning("Could not save nodes file: %s", e)

    # ── TB Edge HTTP ─────────────────────────────────────────────

    async def _tb_auth(self):
        if self._tb_token:
            return True
        try:
            r = await self._http.post(
                f"{self._tb_url}/api/auth/login",
                json={"username": self._tb_user, "password": self._tb_pass},
            )
            if r.status_code == 200:
                self._tb_token = r.json().get("token")
                log.info("TB Edge auth OK")
                return True
        except Exception as e:
            log.warning("TB Edge auth failed: %s", e)
        return False

    async def resolve_device_name(self, prov_key: str) -> str:
        if prov_key in self._prov_cache:
            return self._prov_cache[prov_key]

        if not await self._tb_auth():
            return ""

        try:
            r = await self._http.get(
                f"{self._tb_url}/api/tenant/devices",
                params={"pageSize": 500, "page": 0},
                headers={"X-Authorization": f"Bearer {self._tb_token}"},
            )
            if r.status_code != 200:
                return ""
            devices = r.json().get("data", [])
        except Exception as e:
            log.warning("TB Edge devices query failed: %s", e)
            return ""

        for dev in devices:
            did = dev.get("id", {}).get("id")
            name = dev.get("name", "")
            dtype = dev.get("type", "sensor_sed")
            if not did:
                continue
            try:
                r = await self._http.get(
                    f"{self._tb_url}/api/plugins/telemetry/DEVICE/{did}/values/attributes/SERVER_SCOPE",
                    headers={"X-Authorization": f"Bearer {self._tb_token}"},
                )
                if r.status_code != 200:
                    continue
                attrs = r.json()
                dev_prov = ""
                dev_lat = dev_lng = 0.0
                for attr in attrs:
                    key = attr.get("key", "")
                    val = attr.get("value")
                    if key == "provisioning_key" and val:
                        dev_prov = str(val)
                    elif key == "lat":
                        dev_lat = float(val) if val else 0.0
                    elif key == "lng":
                        dev_lng = float(val) if val else 0.0

                if dev_prov:
                    self._prov_cache[dev_prov] = name
                    self._dev_types_cache[name] = dtype
                    self._dev_pos_cache[name] = (dev_lat, dev_lng)
                    log.info("CACHED prov_key %s -> %s", dev_prov, name)
            except Exception:
                continue

        return self._prov_cache.get(prov_key, "")

    # ── RPC handler ──────────────────────────────────────────────

    def _on_rpc(self, dev, rid, method, params):
        nid = dev.replace("Thread ", "")
        if nid not in self.nodes:
            log.warning("RPC a nodo desconocido: %s", dev)
            return
        log.info("RPC %s -> %s (%s)", method, nid, params)
        if method == "set_valve":
            self.pending_commands[nid] = {"v": params.get("state", 0)}
        elif method in ("set_config", "set_thresholds"):
            self.pending_commands[nid] = {
                "tt": params.get("tt", 0.5),
                "hb": params.get("hb", 300),
            }
        elif method == "start_publishing":
            self.pending_commands[nid] = {"cmd": "start"}
            log.info("RPC START %s", nid)

    # ── Servidor CoAP ────────────────────────────────────────────

    async def start_coap(self):
        root = resource.Site()
        root.add_resource(["readings"], ReadingsResource(self))
        root.add_resource([".well-known", "core"],
                          resource.WKCResource(root.get_resources_as_linkheader()))
        port = self.cfg.get("coap", {}).get("port", 5685)
        await aiocoap.Context.create_server_context(root, bind=("::", port))
        log.info("CoAP [::]:%s /readings", port)

    async def start(self):
        log.info("=" * 50)
        log.info("BRIDGE CoAP Push v3 — provisioning + gateway MQTT")
        log.info("=" * 50)
        self.mqtt.connect(rpc_cb=self._on_rpc)
        await self.start_coap()
        log.info("Esperando POST de nodos...")
        try:
            while self._run:
                await asyncio.sleep(10)
        except KeyboardInterrupt:
            pass


def main():
    p = argparse.ArgumentParser()
    p.add_argument("-c", "--config", default="config.yaml")
    b = Bridge(p.parse_args().config)
    try:
        asyncio.run(b.start())
    except KeyboardInterrupt:
        log.info("Stopped")


if __name__ == "__main__":
    main()
