/*
Illustration of P2P blocking send and receives from for 0 to core 1.
To run: epython p2pcomm.py
*/

from parallel import *

if coreid()==0:
  send(20, 17)
elif coreid()==17:
  print "Got value "+str(recv(0))+" from core 0"
