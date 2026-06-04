#!/usr/bin/env python3
import argparse,asyncio,logging,random,time,cbor2
from aiocoap import resource,Message,Context
from aiocoap.numbers import ContentFormat
log=logging.getLogger(__name__);logging.basicConfig(level=logging.INFO)
class T(resource.Resource):
 def __init__(s,bl=25):super().__init__();s._bl=bl
 async def render_get(s,r):return Message(payload=cbor2.dumps({"t":round(s._bl+random.gauss(0,1.5),1)}),content_format=ContentFormat.CBOR)
class H(resource.Resource):
 def __init__(s,bl=60):super().__init__();s._bl=bl
 async def render_get(s,r):return Message(payload=cbor2.dumps({"h":min(100,max(0,int(s._bl+random.gauss(0,5))))}),content_format=ContentFormat.CBOR)
class V(resource.Resource):
 def __init__(s):super().__init__();s._s=0
 async def render_put(s,r):
  try:s._s=cbor2.loads(r.payload).get("v",s._s)
  except:pass
  return Message(payload=cbor2.dumps({"v":s._s}),content_format=ContentFormat.CBOR)
 async def render_get(s,r):return Message(payload=cbor2.dumps({"v":s._s}),content_format=ContentFormat.CBOR)
class SH(resource.Resource):
 def __init__(s):super().__init__();s._st=time.time()
 async def render_get(s,r):return Message(payload=cbor2.dumps({"batt":random.randint(2800,3300),"rssi":random.randint(-85,-45),"up":int(time.time()-s._st)}),content_format=ContentFormat.CBOR)
class SR(resource.Resource):
 def __init__(s,nid):super().__init__();s._nid=nid
 async def render_put(s,r):
  try:log.info("Reg %s: %s",s._nid,cbor2.loads(r.payload))
  except:pass
  return Message(payload=cbor2.dumps({"registered":True}),content_format=ContentFormat.CBOR)
async def main():
 a=argparse.ArgumentParser();a.add_argument("--node-id",default="NODO-TH-01");a.add_argument("--port",type=int,default=5683);a.add_argument("--temp",type=float,default=25);a.add_argument("--hum",type=int,default=60);a=a.parse_args()
 root=resource.Site()
 for p,cls in [(("env","temp"),T),(("env","hum"),H),(("act","valve"),V),(("sys","health"),SH),(("sys","register"),type("R",(SR,),{"__init__":lambda s:SR.__init__(s,a.node_id)}))]:root.add_resource(p,cls())
 ctx=await Context.create_server_context(root,bind=("::",a.port))
 log.info("Nodo %s port %d",a.node_id,a.port)
 try:await asyncio.Future()
 except:await ctx.shutdown()
if __name__=="__main__":asyncio.run(main())
