import logging,time,requests
log=logging.getLogger(__name__)
class ThreadScanner:
 def __init__(self,cfg):
  self._api=cfg.get("otbr",{}).get("rest_api","http://localhost:8081")
  self._int=cfg.get("otbr",{}).get("scan_interval_s",30);self._last=0;self._known=set()
 def should_scan(self):n=time.time()
  if n-self._last>=self._int:self._last=n;return True
  return False
 def discover(self):
  try:
   r=requests.get(f"{self._api}/devices",timeout=5)
   if r.status_code!=200:return[]
   new=[]
   for d in r.json():
    ea=d.get("extAddr","")
    if ea and ea not in self._known:self._known.add(ea);new.append(d)
   return new
  except:return[]
