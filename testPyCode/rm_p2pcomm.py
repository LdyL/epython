/*
Illustration of remote P2P blocking send and receives
from core 0 to core 17.
from core 17 to core 0.
To run: epython rm_p2pcomm.py
*/

from parallel import *

if coreid()==0:
  send(20, 17)
  recv(17)
elif coreid()==17:
  recv(0)
  send(21,0)
  print "Got value "+str(recv(0))+" from core 0"
