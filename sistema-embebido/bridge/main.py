#!/usr/bin/env python3
import argparse,asyncio,logging,time,yaml,cbor2,json,sys
from mqtt.publisher import MQTTPublisher
from mqtt.subscriber import MQTTSubscriber

logging.basicConfig(level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
log = logging.getLogger("bridge")

import aiocoap
from aiocoap import resource, CHANGED, CONTENT

# ── CoAP Resource /readings ───────────────────────────────────────
class ReadingsResource(resource.Resource):
    def __init__(self, bridge):
        super().__init__()
        self._bridge = bridge

    async def render_post(self, request):
        try:
            data = cbor2.loads(request.payload)
        except Exception:
            log.warning("CBOR decode failed")
            return aiocoap.Message(code=aiocoap.BAD_REQUEST)

        now_ms = int(time.time() * 1000)
        nid = data.get("id", "unknown")

        # ── Separar telemetría (time-series) de atributos (estáticos) ──
        telemetry_keys = {"t", "h", "b", "r", "u"}
        attr_keys      = {"zn", "tp", "x", "y", "v", "lat", "lng"}

        telemetry_vals = {}
        attr_vals      = {}

        for k, v in data.items():
            if k in telemetry_keys:
                telemetry_vals[k] = v
            elif k in attr_keys:
                attr_vals[k] = v

        # Renombrar para ThingsBoard
        tb_telemetry = {}
        if "t" in telemetry_vals: tb_telemetry["temperature"] = round(telemetry_vals["t"], 1)
        if "h" in telemetry_vals: tb_telemetry["humidity"]    = int(telemetry_vals["h"])
        if "b" in telemetry_vals: tb_telemetry["battery"]     = int(telemetry_vals["b"])
        if "r" in telemetry_vals: tb_telemetry["rssi"]        = int(telemetry_vals["r"])
        if "u" in telemetry_vals: tb_telemetry["uptime"]      = int(telemetry_vals["u"])

        tb_attrs = {}
        if "zn" in attr_vals: tb_attrs["zone"]    = attr_vals["zn"]
        if "tp" in attr_vals: tb_attrs["type"]    = attr_vals["tp"]
        if "x"   in attr_vals: tb_attrs["pos_x"]   = round(attr_vals["x"], 1)
        if "y"   in attr_vals: tb_attrs["pos_y"]   = round(attr_vals["y"], 1)
        if "lat" in attr_vals: tb_attrs["lat"]     = round(attr_vals["lat"], 6)
        if "lng" in attr_vals: tb_attrs["lng"]     = round(attr_vals["lng"], 6)
        if "v"   in attr_vals: tb_attrs["version"] = attr_vals["v"]

        # ── Publicar MQTT ──
        if tb_telemetry:
            self._bridge.mqtt.telemetry(nid, now_ms, tb_telemetry)
            log.info(">>> %s t=%.1f°C h=%d%% batt=%dmV rssi=%d uptime=%ds",
                     nid,
                     tb_telemetry.get("temperature", 0),
                     tb_telemetry.get("humidity", 0),
                     tb_telemetry.get("battery", 0),
                     tb_telemetry.get("rssi", 0),
                     tb_telemetry.get("uptime", 0))

        # ── Registrar nodo + atributos (solo la primera vez) ──
        is_new = nid not in self._bridge.nodes
        if is_new:
            self._bridge.nodes[nid] = {
                "id": nid,
                "zone": tb_attrs.get("zone", ""),
                "type": tb_attrs.get("type", "TH"),
                "pos_x": tb_attrs.get("pos_x", 0),
                "pos_y": tb_attrs.get("pos_y", 0)
            }
            self._bridge.mqtt.connect_dev(nid, tb_attrs.get("type", "Sensor"))
            log.info("REGISTER %s → zone=%s type=%s pos=(%.1f,%.1f)",
                     nid,
                     tb_attrs.get("zone", "?"),
                     tb_attrs.get("type", "?"),
                     tb_attrs.get("pos_x", 0),
                     tb_attrs.get("pos_y", 0))

        # Publicar atributos (cada vez que cambien o primera vez)
        if tb_attrs and (is_new or self._bridge._attrs_changed(nid, tb_attrs)):
            self._bridge.mqtt.attributes(nid, tb_attrs)

        # ── Downlink: comandos pendientes ──
        cmd = self._bridge.pending_commands.pop(nid, None)
        resp = b'\xa0'   # CBOR {} = sin comando
        if cmd:
            log.info("DOWNLINK %s → %s", nid, cmd)
            try:
                resp = cbor2.dumps(cmd)
            except Exception:
                resp = b'\xa0'

        return aiocoap.Message(code=CONTENT, payload=resp,
                               content_format=60)

# ── Bridge ────────────────────────────────────────────────────────
class Bridge:
    def __init__(self, path):
        with open(path) as f: self.cfg = yaml.safe_load(f)
        self.mqtt = MQTTPublisher(self.cfg)
        self.rpc   = MQTTSubscriber(self.cfg, self._on_rpc)
        self.nodes = {}
        self.pending_commands = {}
        self._node_attrs = {}  # track last known attrs to detect changes
        self._run = True

    def _attrs_changed(self, nid, attrs):
        old = self._node_attrs.get(nid, {})
        self._node_attrs[nid] = attrs
        return old != attrs

    def _on_rpc(self, dev, rid, method, params):
        nid = dev.replace("Thread ", "")
        if nid not in self.nodes:
            log.warning("RPC a nodo desconocido: %s", dev)
            return
        log.info("RPC %s → %s (%s)", method, nid, params)
        if method == "set_valve":
            self.pending_commands[nid] = {"v": params.get("state", 0)}
        elif method == "set_thresholds":
            self.pending_commands[nid] = {
                "tt": params.get("tt", 0.5),
                "ht": params.get("ht", 3),
                "hb": params.get("hb", 45)
            }

    async def start_coap_server(self):
        root = resource.Site()
        root.add_resource(["readings"], ReadingsResource(self))
        root.add_resource([".well-known", "core"],
            resource.WKCResource(root.get_resources_as_linkheader()))
        await aiocoap.Context.create_server_context(root, bind=("::", 5685))
        log.info("CoAP Server [::]:5685 /readings")

    async def start(self):
        log.info("=" * 50)
        log.info("BRIDGE CoAP Server — modo real + Attributes + RPC")
        log.info("=" * 50)
        self.mqtt.connect()
        await self.start_coap_server()
        log.info("Esperando POSTs de nodos Thread...")
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
