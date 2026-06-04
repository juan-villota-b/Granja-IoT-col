from dataclasses import dataclass
from typing import Optional
@dataclass
class ThreadNode:
 node_id:str;zone:str;ipv6:Optional[str]=None;ext_addr:Optional[str]=None;pos_x:float=0;pos_y:float=0;registered:bool=False;is_valve:bool=False
 @property
 def device_name(self):return f"Thread {self.node_id}"
 @property
 def device_type(self):return"Actuator"if self.is_valve else"Sensor"
