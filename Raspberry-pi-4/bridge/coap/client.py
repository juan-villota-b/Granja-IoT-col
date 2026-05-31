import asyncio,logging,time
log=logging.getLogger(__name__)
class CoapClient:
 def __init__(self,cfg):self._proto=None
 async def _ensure(self):
  if self._proto is None:
   import aiocoap;self._proto=await aiocoap.Context.create_client_context()
 async def observe(self,ipv6,path,on_data):
  import aiocoap,cbor2
  from aiocoap.numbers import ContentFormat
  await self._ensure()
  req=aiocoap.Message(code=aiocoap.GET,uri=f"coap://[{ipv6}]:5683{path}")
  req.opt.observe=0;req.opt.content_format=ContentFormat.CBOR
  try:
   resp=await self._proto.request(req).response;log.info("Observe %s started",req.uri)
   async for n in resp.observation:
    if n.code.is_successful():
     try:on_data(ipv6,path,cbor2.loads(n.payload),time.time())
     except Exception as e:log.warning("CBOR: %s",e)
  except Exception as e:log.warning("Observe: %s",e)
 async def put(self,ipv6,path,payload,timeout=10):
  import aiocoap;await self._ensure();uri=f"coap://[{ipv6}]:5683{path}"
  try:
   resp=await asyncio.wait_for(self._proto.request(aiocoap.Message(code=aiocoap.PUT,uri=uri,payload=payload)).response,timeout=timeout)
   return resp.code
  except:return None
 async def get(self,ipv6,path,timeout=5):
  import aiocoap;await self._ensure();uri=f"coap://[{ipv6}]:5683{path}"
  try:
   resp=await asyncio.wait_for(self._proto.request(aiocoap.Message(code=aiocoap.GET,uri=uri)).response,timeout=timeout)
   return resp.payload if resp.code.is_successful() else None
  except:return None
