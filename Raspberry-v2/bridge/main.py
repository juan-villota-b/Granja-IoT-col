#!/usr/bin/env python3

import argparse, asyncio, json, logging, os, time, yaml, cbor2

DEFAULT_COORDS = {
    "NODO-SENSOR-1": (5.029056, -75.472472),
    "NODO-SENSOR-2": (5.029065, -75.472472),
    "NODO-SENSOR-3": (5.029056, -75.472463),
    "NODO-SENSOR-4": (5.029047, -75.472472),
}

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s"
)
log = logging.getLogger("bridge")

import aiocoap
from aiocoap import resource, CONTENT, BAD_REQUEST

from mqtt.publisher  import MQTTPublisher
from mqtt.subscriber import MQTTSubscriber

NODES_FILE = "/data/registered_nodes.json"


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
        nid = data.get("id", "unknown")
        if nid == "unknown":
            return aiocoap.Message(code=BAD_REQUEST)

        is_new = nid not in self._bridge.nodes
        if is_new:
            ntype = data.get("tp", "sensor_sed")
            lat = data.get("lat", 0.0)
            lng = data.get("lng", 0.0)
            self._bridge.nodes[nid] = {
                "id": nid, "ipv6": ipv6,
                "zone": data.get("zn", ""),
                "type": ntype,
                "lat": lat,
                "lng": lng,
                "attrs_sent": False,
            }
            self._bridge._save_nodes()
            log.info("AUTO-REGISTER %s from %s", nid, ipv6)

        node = self._bridge.nodes[nid]
        if not node.get("attrs_sent"):
            dlat, dlng = DEFAULT_COORDS.get(nid, (0.0, 0.0))
            lat = data.get("lat") or node.get("lat") or dlat
            lng = data.get("lng") or node.get("lng") or dlng
            attrs = {
                "zone": data.get("zn", node.get("zone", "")),
                "type": data.get("tp", node.get("type", "sensor_sed")),
                "version": data.get("v", ""),
                "lat": round(lat, 6),
                "lng": round(lng, 6),
            }
            self._bridge.mqtt.attributes(nid, attrs)
            self._bridge.nodes[nid]["attrs_sent"] = True
            self._bridge.nodes[nid]["lat"] = lat
            self._bridge.nodes[nid]["lng"] = lng
            self._bridge._save_nodes()
            log.info("ATTRS %s lat=%.4f lng=%.4f", nid, lat, lng)

        tb_telemetry = {}
        if "t" in data:
            tb_telemetry["temperature"] = round(data["t"], 1)
            log_msg = "%s t=%.1f°C"
            log_args = [nid, tb_telemetry["temperature"]]
        elif "h" in data:
            tb_telemetry["humidity"] = int(data["h"])
            log_msg = "%s h=%d%%"
            log_args = [nid, tb_telemetry["humidity"]]
        elif "l" in data:
            tb_telemetry["light"] = int(data["l"])
            log_msg = "%s l=%d lux"
            log_args = [nid, tb_telemetry["light"]]
        if tb_telemetry:
            if "r" in data:
                tb_telemetry["rssi"] = int(data["r"])
            if "u" in data:
                tb_telemetry["uptime"] = int(data["u"])
            self._bridge.mqtt.telemetry(nid, now_ms, tb_telemetry)
            log.info(">>> " + log_msg + " rssi=%d up=%ds",
                     *(log_args + [tb_telemetry.get("rssi", 0), tb_telemetry.get("uptime", 0)]))

        cmd = self._bridge.pending_commands.pop(nid, None)
        resp = b'\xa0'
        if cmd:
            log.info("DOWNLINK %s → %s", nid, cmd)
            try:
                resp = cbor2.dumps(cmd)
            except Exception:
                resp = b'\xa0'

        return aiocoap.Message(code=CONTENT, payload=resp, content_format=60)


class Bridge:
    def __init__(self, path):
        with open(path) as f:
            self.cfg = yaml.safe_load(f)
        self.mqtt = MQTTPublisher(self.cfg)
        self.rpc = MQTTSubscriber(self.cfg, self._on_rpc)
        self.nodes = {}
        self.pending_commands = {}
        self._run = True
        self._load_nodes()

    def _load_nodes(self):
        if not os.path.exists(NODES_FILE):
            log.info("No registered nodes file found, starting fresh")
            return
        try:
            with open(NODES_FILE) as f:
                self.nodes = json.load(f)
            log.info("Loaded %d previously registered nodes", len(self.nodes))
            for nid in self.nodes:
                log.info("  ➜ %s", nid)
        except Exception as e:
            log.warning("Could not load nodes file: %s", e)
            self.nodes = {}

    def _save_nodes(self):
        try:
            os.makedirs(os.path.dirname(NODES_FILE), exist_ok=True)
            with open(NODES_FILE, "w") as f:
                json.dump(self.nodes, f, indent=2)
        except Exception as e:
            log.warning("Could not save nodes file: %s", e)

    def _on_rpc(self, dev, rid, method, params):
        nid = dev.replace("Thread ", "")
        if nid not in self.nodes:
            log.warning("RPC a nodo desconocido: %s", dev)
            return
        log.info("RPC %s → %s (%s)", method, nid, params)
        if method == "set_valve":
            self.pending_commands[nid] = {"v": params.get("state", 0)}
        elif method in ("set_config", "set_thresholds"):
            self.pending_commands[nid] = {
                "tt": params.get("tt", 0.5),
                "hb": params.get("hb", 300),
            }

    async def start_coap(self):
        root = resource.Site()
        root.add_resource(["readings"], ReadingsResource(self))
        root.add_resource([".well-known", "core"],
                          resource.WKCResource(root.get_resources_as_linkheader()))
        port = self.cfg.get("coap", {}).get("port", 5685)
        await aiocoap.Context.create_server_context(root, bind=("::", port))
        log.info("CoAP [::]:%s /readings (push pasivo, con auto-registro)", port)

    async def start(self):
        log.info("=" * 50)
        log.info("BRIDGE CoAP Push v2 — auto-registro + downlink piggyback")
        log.info("=" * 50)
        self.mqtt.connect()
        await self.start_coap()
        log.info("Esperando POST de nodos (telemetria + auto-registro)...")
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
