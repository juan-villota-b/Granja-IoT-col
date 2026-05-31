import json,logging,paho.mqtt.client as mqtt
log=logging.getLogger(__name__)
class MQTTPublisher:
 def __init__(self,cfg):
  c=cfg.get("mqtt",{});self._host=c.get("host","localhost");self._port=c.get("port",1883)
  self._tt=c.get("telemetry_topic","v1/gateway/telemetry");self._ct=c.get("connect_topic","v1/gateway/connect")
  self._cl=mqtt.Client()
  u=c.get("username","");p=c.get("password","")
  if u:self._cl.username_pw_set(u,p)
  self._cl.on_connect=lambda c,u,f,rc:rc==0 and log.info("MQTT OK")
  self._connected=False
 def connect(self):
  try:self._cl.connect(self._host,self._port,60);self._cl.loop_start();self._connected=True
  except Exception as e:log.warning("MQTT: %s",e)
 def telemetry(self,dev,ts,vals):
  if not self._connected:return
  self._cl.publish(self._tt,json.dumps({dev:[{"ts":ts,"values":vals}]}),qos=1)
 def connect_dev(self,dev,typ="default"):
  if not self._connected:return
  self._cl.publish(self._ct,json.dumps({"device":dev,"type":typ}),qos=1)
