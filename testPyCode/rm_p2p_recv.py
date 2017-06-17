/*
Illustration of P2P blocking send from core 17 to core 0.
*/

from parallel import *

if coreid()==0:
  print "Got value "+str(recv(17))+" form core 17"
