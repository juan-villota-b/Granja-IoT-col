#!/usr/bin/env python3
import argparse,asyncio,logging,time,yaml,cbor2,sys
from mqtt.publisher import MQTTPublisher
from mqtt.subscriber import MQTTSubscriber
from downlink.handler import DownlinkHandler
logging.basicConfig(level=logging.INFO,format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
log=logging.getLogger("bridge")

class CoAPReader:
    def __init__(self):self._ctx=None
    async def ensure(self):
        if self._ctx is None:
            import aiocoap;self._ctx=await aiocoap.Context.create_client_context()
            log.info("CoAP context created")
    async def get_cbor(self,host,port,path):
        import aiocoap;await self.ensure()
        uri=f"coap://[{host}]:{port}{path}"
        try:
            r=await asyncio.wait_for(self._ctx.request(aiocoap.Message(code=aiocoap.GET,uri=uri)).response,timeout=5)
            if r.code.is_successful() and r.payload:return cbor2.loads(r.payload)
        except:pass
        return None
    async def put_cbor(self,host,port,path,data):
        import aiocoap;await self.ensure()
        uri=f"coap://[{host}]:{port}{path}"
        try:
            r=await asyncio.wait_for(self._ctx.request(aiocoap.Message(code=aiocoap.PUT,uri=uri,payload=cbor2.dumps(data))).response,timeout=5)
            return r.code.is_successful()
        except:return False

class Bridge:
    def __init__(self,path):
        with open(path)as f:self.cfg=yaml.safe_load(f)
        self.coap=CoAPReader()
        self.mqtt=MQTTPublisher(self.cfg)
        self.rpc=MQTTSubscriber(self.cfg,self._on_rpc)
        self.dl=DownlinkHandler({},self.coap,self.mqtt)
        self.nodes={};self._run=True

    def _on_rpc(self,dev,rid,method,params):
        n=next((n for nid,n in self.nodes.items() if f"Thread {nid}"==dev or nid==dev),None)
        if not n:return
        log.info("RPC %s -> %s (port %d)",method,n["id"],n["port"])
        if method=="set_valve":
            asyncio.run_coroutine_threadsafe(self.coap.put_cbor("::1",n["port"],"/act/valve",{"v":params.get("state",0)}),asyncio.get_event_loop())

    async def _discover(self):
        sim=self.cfg.get("simulation",{})
        if not sim.get("enabled"):return
        for nd in sim.get("nodes",[]):
            nid=nd["node_id"];port=nd.get("coap_port",15683)
            if nid in self.nodes:continue
            h=await self.coap.get_cbor("::1",port,"/sys/health")
            if h:
                self.nodes[nid]={"id":nid,"zone":nd.get("zone",""),"port":port,"type":nd.get("type","TH")}
                self.mqtt.connect_dev(f"Thread {nid}",nd.get("type","Sensor"))
                log.info(">>> CONNECTED: %s (port %d, batt=%d)",nid,port,h.get("batt",0))

    async def _read_and_publish(self):
        now_ms=int(time.time()*1000)
        for nid,n in list(self.nodes.items()):
            port=n["port"]
            t=await self.coap.get_cbor("::1",port,"/env/temp")
            h=await self.coap.get_cbor("::1",port,"/env/hum")
            s=await self.coap.get_cbor("::1",port,"/sys/health")
            if t or h or s:
                vals={}
                if t:vals["temperature"]=round(t.get("t",0),1)
                if h:vals["humidity"]=h.get("h",0)
                if s:vals["battery"]=s.get("batt",0);vals["rssi"]=s.get("rssi",0);vals["uptime"]=s.get("up",0)
                self.mqtt.telemetry(f"Thread {nid}",now_ms,vals)
                log.info(">>> %s: t=%.1f h=%d batt=%d",nid,vals.get("temperature",0),vals.get("humidity",0),vals.get("battery",0))

    async def start(self):
        log.info("="*50);log.info("BRIDGE STARTING - simulation mode");log.info("="*50)
        self.mqtt.connect()
        await self._discover()
        log.info("Discovered %d nodes",len(self.nodes))
        while self._run:
            try:
                await self._read_and_publish()
                await asyncio.sleep(5)
                if len(self.nodes)<5:await self._discover()
            except KeyboardInterrupt:break
            except Exception as e:log.error("Loop: %s",e)

def main():
    p=argparse.ArgumentParser();p.add_argument("-c","--config",default="config.yaml")
    b=Bridge(p.parse_args().config)
    try:asyncio.run(b.start())
    except KeyboardInterrupt:log.info("Stopped")
if __name__=="__main__":main()
