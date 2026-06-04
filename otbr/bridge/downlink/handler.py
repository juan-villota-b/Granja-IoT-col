import asyncio,logging
log=logging.getLogger(__name__)
class DownlinkHandler:
 def __init__(self,nodes,coap,mqtt):
  self._nodes=nodes;self._coap=coap;self._loop=asyncio.new_event_loop()
 def handle(self,dev,rid,method,params):
  n=next((n for nid,n in self._nodes.items() if f"Thread {nid}"==dev or nid==dev),None)
  if not n:log.warning("Unknown device: %s",dev);return
  port=n["port"]
  log.info("RPC: %s %s port %d",method,dev,port)
  if method=="set_valve":
   asyncio.run_coroutine_threadsafe(self._coap.put_cbor("::1",port,"/act/valve",{"v":params.get("state",0)}),self._loop)
  elif method=="get_health":
   asyncio.run_coroutine_threadsafe(self._coap.get_cbor("::1",port,"/sys/health"),self._loop)
