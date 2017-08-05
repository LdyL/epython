/*
Illustration of cross P2P blocking send from core 0 to core 17
and from core 16 to core 1.
*/

from parallel import *

if coreid()==0:
  val=20
  print "[core 0]sending value "+str(val)+" to core 17"
  send(val, 17)
if coreid()==17:
  print "[core 17]Got value "+str(recv(0))+" from core 0"
if coreid()==16:
  val=30.0
  print "[core 16]sending value "+str(val)+" to core 1"
  send(val, 1)
if coreid()==1:
  print "[core 1]Got value "+str(recv(16))+" from core 16"
