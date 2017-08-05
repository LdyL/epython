/*
Illustration of cross P2P blocking message passing from core 1 to core 18
and from core 17 to core 2.
*/

from parallel import *

if coreid()==1:
  val=20
  print "[core 1]sending value "+str(val)+" to core 18"
  send(val, 18)
if coreid()==18:
  print "[core 18]Got value "+str(recv(0))+" from core 0"
if coreid()==17:
  val=30.0
  print "[core 17]sending value "+str(val)+" to core 2"
  send(val, 2)
if coreid()==2:
  print "[core 2]Got value "+str(recv(17))+" from core 17"
