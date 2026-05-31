import logging,cbor2
log=logging.getLogger(__name__)
class RegistrationClient:
 def __init__(self,coap):self._coap=coap
 async def register(self,ipv6,nid,zone,x,y):
  p=cbor2.dumps({"id":nid,"zone":zone,"x":x,"y":y})
  c=await self._coap.put(ipv6,"/sys/register",p,10)
  if c and c.is_successful():log.info("Reg %s OK",nid);return True
  return False
 async def health(self,ipv6):
  p=await self._coap.get(ipv6,"/sys/health")
  if p:
   try:return cbor2.loads(p)
   except:pass
  return None
