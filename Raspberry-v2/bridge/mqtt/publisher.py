import json, logging, paho.mqtt.client as mqtt, time
log = logging.getLogger(__name__)


class MQTTPublisher:
    def __init__(self, cfg):
        c = cfg.get("mqtt", {})
        self._host = c.get("host", "localhost")
        self._port = c.get("port", 1883)
        self._gt_topic = c.get("telemetry_topic", "v1/gateway/telemetry")
        self._ct = c.get("connect_topic", "v1/gateway/connect")
        self._at = c.get("attributes_topic", "v1/gateway/attributes")
        self._dt = c.get("disconnect_topic", "v1/gateway/disconnect")
        self._rpc_topic = c.get("rpc_topic", "v1/gateway/rpc")
        self._mode = cfg.get("mqtt_mode", "gateway")
        self._gw_cl = None
        self._connected = False
        self._rpc_cb = None

        if self._mode == "gateway":
            self._gw_cl = mqtt.Client()
            u = c.get("username", "")
            p = c.get("password", "")
            if u:
                self._gw_cl.username_pw_set(u, p)
            self._gw_cl.on_connect = self._on_connect
            self._gw_cl.on_message = self._on_message
            self._gw_cl.on_disconnect = self._on_disconnect

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self._connected = True
            log.info("MQTT GW OK")
            self._gw_cl.subscribe(self._rpc_topic, qos=1)
        else:
            self._connected = False
            log.warning("MQTT GW fail rc=%d", rc)

    def _on_message(self, client, userdata, msg):
        try:
            d = json.loads(msg.payload)
            dev = d.get("device")
            if dev and self._rpc_cb:
                data = d.get("data", {})
                method = data.get("method")
                params = data.get("params", {})
                mid = d.get("id")
                self._rpc_cb(dev, mid, method, params)
        except Exception as e:
            log.warning("RPC parse fail: %s", e)

    def _on_disconnect(self, client, userdata, rc):
        self._connected = False
        if rc != 0:
            log.warning("MQTT GW disconnect rc=%d", rc)

    def connect(self, rpc_cb=None):
        self._rpc_cb = rpc_cb
        if self._mode == "gateway" and self._gw_cl:
            try:
                self._gw_cl.connect(self._host, self._port, 60)
                self._gw_cl.loop_start()
                log.info("MQTT GW connecting to %s:%s", self._host, self._port)
            except Exception as e:
                log.warning("MQTT connect fail: %s", e)

    def telemetry(self, dev, ts, values):
        if self._gw_cl and self._connected:
            self._gw_cl.publish(self._gt_topic, json.dumps({dev: [{"ts": ts, "values": values}]}), qos=1)

    def attributes(self, dev, values):
        if self._gw_cl and self._connected:
            self._gw_cl.publish(self._at, json.dumps({dev: values}), qos=1)

    def connect_dev(self, dev, typ="default"):
        if self._gw_cl and self._connected:
            self._gw_cl.publish(self._ct, json.dumps({"device": dev, "type": typ}), qos=1)
            log.info("MQTT connect_dev: %s type=%s", dev, typ)
        else:
            log.warning("MQTT not connected, cannot connect_dev %s", dev)

    def disconnect_dev(self, dev):
        if self._gw_cl and self._connected:
            self._gw_cl.publish(self._dt, json.dumps({"device": dev}), qos=1)
