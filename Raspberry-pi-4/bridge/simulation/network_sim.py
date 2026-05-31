#!/usr/bin/env python3
import argparse,asyncio,logging,sys,os
logging.basicConfig(level=logging.INFO)
log=logging.getLogger("net_sim")
async def main():
 a=argparse.ArgumentParser();a.add_argument("--nodes",type=int,default=5);a.add_argument("--base-port",type=int,default=5683);a=a.parse_args()
 cf=[("NODO-TH-01","ZONA-A",25,60),("NODO-TH-02","ZONA-A",28,55),("NODO-TH-03","ZONA-B",22,65),("NODO-TH-SED-01","ZONA-B",20,70),("NODO-VALVE-01","ZONA-A",26,58)]
 ps=[]
 for i in range(min(a.nodes,len(cf))):
  nid,z,t,h=cf[i];p=a.base_port+i
  env=os.environ.copy();env["PYTHONPATH"]=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
  proc=await asyncio.create_subprocess_exec(sys.executable,"-m","simulation.node_sim","--node-id",nid,"--port",str(p),"--temp",str(t),"--hum",str(h),cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
  ps.append(proc);log.info("Started %s port %d",nid,p)
 log.info("All %d nodes running. Press Ctrl+C to stop.",len(ps))
 try:await asyncio.Event().wait()
 except:[p.terminate() for p in ps];log.info("All stopped")
if __name__=="__main__":asyncio.run(main())
