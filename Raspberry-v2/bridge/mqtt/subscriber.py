import json, logging, paho.mqtt.client as mqtt
log = logging.getLogger(__name__)

class MQTTSubscriber:
    def __init__(self, cfg, on_rpc):
        c = cfg.get("mqtt", {})
        self._t = c.get("rpc_topic", "v1/gateway/rpc")
        self._cb = on_rpc
        self._cl = mqtt.Client()
        self._cl.username_pw_set(c.get('username', ''), c.get('password', ''))
        self._cl.on_connect = lambda c2, u, f2, rc: rc == 0 and c2.subscribe(self._t, qos=1)
        self._cl.on_message = lambda c, u, m: exec(
            'try:\n d=json.loads(m.payload);dev=d.get("device");mid=d.get("id");m=d.get("data",{}).get("method");p=d.get("data",{}).get("params",{})\n if dev and m:self._cb(dev,mid,m,p)\nexcept Exception as e:log.warning("RPC: %s",e)'
        )
        try:
            self._cl.connect(c.get("host", "localhost"), c.get("port", 1883), 60)
            self._cl.loop_start()
            log.info("RPC subscriber ready")
        except Exception as e:
            log.warning("RPC sub fail: %s", e)
