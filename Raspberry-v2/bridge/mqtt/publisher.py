import json,logging,paho.mqtt.client as mqtt
log=logging.getLogger(__name__)

class MQTTPublisher:
 def __init__(self,cfg):
  c=cfg.get("mqtt",{});self._host=c.get("host","localhost");self._port=c.get("port",1883)
  self._gt_topic=c.get("telemetry_topic","v1/gateway/telemetry")
  self._ct=c.get("connect_topic","v1/gateway/connect")
  self._at=c.get("attributes_topic","v1/gateway/attributes")
  self._dt=c.get("disconnect_topic","v1/gateway/disconnect")
  self._tokens=cfg.get("device_tokens",{})
  self._mode=cfg.get("mqtt_mode","direct")  # "direct" or "gateway"
  self._gw_cl=None
  self._dev_clients={}
  self._connected=False

  if self._mode=="gateway":
   self._gw_cl=mqtt.Client()
   u=c.get("username","");p=c.get("password","")
   if u:self._gw_cl.username_pw_set(u,p)
   self._gw_cl.on_connect=lambda c,u,f,rc:rc==0 and log.info("MQTT GW OK")

 def connect(self):
  if self._mode=="gateway" and self._gw_cl:
   try:self._gw_cl.connect(self._host,self._port,60);self._gw_cl.loop_start();self._connected=True
   except Exception as e:log.warning("MQTT: %s",e)
  else:
   self._connected=True
   log.info("MQTT direct mode — devices will connect on demand")

 def _ensure_dev_client(self,dev):
  if dev in self._dev_clients:return self._dev_clients[dev]
  token=self._tokens.get(dev)
  if not token:
   log.warning("MQTT no token for %s, using gateway",dev)
   return None
  cl=mqtt.Client()
  cl.username_pw_set(token,'')
  cl.on_connect=lambda c,u,f,rc:(
   log.info("MQTT %s OK" % dev) if rc==0 else log.error("MQTT %s fail rc=%d"%(dev,rc)))
  try:
   cl.connect(self._host,self._port,60)
   cl.loop_start()
   self._dev_clients[dev]=cl
   return cl
  except Exception as e:
   log.error("MQTT %s: %s",dev,e)
   return None

 def telemetry(self,dev,ts,vals):
  if self._mode=="direct":
   cl=self._ensure_dev_client(dev)
   if cl:
    cl.publish("v1/devices/me/telemetry",json.dumps({"ts":ts,"values":vals}),qos=1)
  elif self._gw_cl:
   self._gw_cl.publish(self._gt_topic,json.dumps({dev:[{"ts":ts,"values":vals}]}),qos=1)

 def attributes(self,dev,vals):
  if self._mode=="direct":
   cl=self._ensure_dev_client(dev)
   if cl:
    cl.publish("v1/devices/me/attributes",json.dumps(vals),qos=1)
  elif self._gw_cl:
   self._gw_cl.publish(self._at,json.dumps({dev:vals}),qos=1)

 def connect_dev(self,dev,typ="default"):
  if self._mode=="direct":
   self._ensure_dev_client(dev)
  elif self._gw_cl:
   self._gw_cl.publish(self._ct,json.dumps({"device":dev,"type":typ}),qos=1)
