/*
Illustration of P2P blocking send from core 0 to core 17.
*/

from parallel import *

if coreid()==0:
  send(20, 17)
if coreid()==17:
  print "Got value "+str(recv(0))+" form core 0"
