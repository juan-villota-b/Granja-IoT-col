#!/usr/bin/env python3

import argparse, asyncio, logging, time, yaml, cbor2

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s"
)
log = logging.getLogger("bridge")

import aiocoap
from aiocoap import resource, CONTENT, BAD_REQUEST

from mqtt.publisher  import MQTTPublisher
from mqtt.subscriber import MQTTSubscriber


# ═══════════════════════════════════════════════════════════════════
#  CoAP Resource /readings — registro + telemetria unificados
# ═══════════════════════════════════════════════════════════════════

class ReadingsResource(resource.Resource):
    def __init__(self, bridge):
        super().__init__()
        self._bridge = bridge

    async def render_post(self, request):
        """CON POST /readings — auto-registro + telemetria + downlink."""
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

        # ── Extraer id del nodo ──
        nid = data.get("id", "unknown")
        if nid == "unknown":
            return aiocoap.Message(code=BAD_REQUEST)

        # ── Auto-registro si Bridge se reinicio ──
        is_new = nid not in self._bridge.nodes
        if is_new:
            ntype = data.get("tp", "sensor_sed")
            self._bridge.nodes[nid] = {
                "id": nid, "ipv6": ipv6,
                "zone": data.get("zn", ""),
                "type": ntype,
                "lat": data.get("lat", 0.0),
                "lng": data.get("lng", 0.0),
            }
            self._bridge.mqtt.connect_dev(nid, ntype)
            tb_attrs = {
                "zone": data.get("zn", ""),
                "type": ntype,
                "version": data.get("v", ""),
                "lat": round(data.get("lat", 0.0), 6),
                "lng": round(data.get("lng", 0.0), 6),
            }
            self._bridge.mqtt.attributes(nid, tb_attrs)
            log.info("AUTO-REGISTER %s from %s (Bridge recovery)", nid, ipv6)

        # ── Telemetria ──
        if "t" in data:
            tb_telemetry = {
                "temperature": round(data["t"], 1),
            }
            if "r" in data:
                tb_telemetry["rssi"] = int(data["r"])
            if "u" in data:
                tb_telemetry["uptime"] = int(data["u"])
            self._bridge.mqtt.telemetry(nid, now_ms, tb_telemetry)
            log.info(">>> %s t=%.1f°C rssi=%d up=%ds",
                     nid,
                     tb_telemetry.get("temperature", 0),
                     tb_telemetry.get("rssi", 0),
                     tb_telemetry.get("uptime", 0))

        # ── Downlink: comandos pendientes ──
        cmd = self._bridge.pending_commands.pop(nid, None)
        resp = b'\xa0'
        if cmd:
            log.info("DOWNLINK %s → %s", nid, cmd)
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
        self.mqtt  = MQTTPublisher(self.cfg)
        self.rpc   = MQTTSubscriber(self.cfg, self._on_rpc)
        self.nodes = {}
        self.pending_commands = {}
        self._node_attrs = {}
        self._run = True

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
