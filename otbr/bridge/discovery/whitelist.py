import logging
from models.device import ThreadNode
log=logging.getLogger(__name__)
class Whitelist:
 def __init__(self,cfg):
  self._enabled=cfg.get("whitelist",{}).get("enabled",True)
  self._nodes={}
  for n in cfg.get("whitelist",{}).get("nodes",[]):
   node=ThreadNode(node_id=n["node_id"],zone=n["zone"],pos_x=n.get("pos_x",0),pos_y=n.get("pos_y",0),is_valve=n.get("valve",False))
   self._nodes[node.node_id]=node
  log.info("Whitelist: %d nodes",len(self._nodes))
 def is_authorized(self,nid):return True if not self._enabled else nid in self._nodes
 def get_node(self,nid):return self._nodes.get(nid)
