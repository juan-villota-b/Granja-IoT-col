import json, logging, paho.mqtt.client as mqtt
log = logging.getLogger(__name__)

class MQTTPublisher:
    def __init__(self, cfg):
        c = cfg.get("mqtt", {})
        self._host = c.get("host", "localhost")
        self._port = c.get("port", 1883)
        self._gt_topic = c.get("telemetry_topic", "v1/gateway/telemetry")
        self._ct = c.get("connect_topic", "v1/gateway/connect")
        self._at = c.get("attributes_topic", "v1/gateway/attributes")
        self._tokens = cfg.get("device_tokens", {})
        self._mode = cfg.get("mqtt_mode", "direct")
        self._gw_cl = None
        self._connected = False

        if self._mode == "gateway":
            self._gw_cl = mqtt.Client()
            u = c.get("username", "")
            p = c.get("password", "")
            if u:
                self._gw_cl.username_pw_set(u, p)
            self._gw_cl.on_connect = lambda c, u, f, rc: rc == 0 and log.info("MQTT GW OK")

    def connect(self):
        if self._mode == "gateway" and self._gw_cl:
            try:
                self._gw_cl.connect(self._host, self._port, 60)
                self._gw_cl.loop_start()
                self._connected = True
            except Exception as e:
                log.warning("MQTT: %s", e)
        else:
            self._connected = True
            log.info("MQTT direct mode")

    def telemetry(self, dev, ts, vals):
        if self._mode == "direct":
            token = self._tokens.get(dev)
            if token:
                cl = mqtt.Client(client_id=dev)
                cl.username_pw_set(token, '')
                try:
                    cl.connect(self._host, self._port, 10)
                    cl.loop_start()
                    cl.publish("v1/devices/me/telemetry", json.dumps({"ts": ts, "values": vals}), qos=1)
                except Exception as e:
                    log.error("MQTT %s: %s", dev, e)
        elif self._gw_cl:
            self._gw_cl.publish(self._gt_topic, json.dumps({dev: [{"ts": ts, "values": vals}]}), qos=1)
