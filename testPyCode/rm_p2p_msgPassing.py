/*
Illustration of P2P blocking send from core 0 to core 17.
*/

from parallel import *

if coreid()==0:
  val=20.0
  print "[core 0]sending value "+str(val)+" to core 17"
  send(val, 17)
if coreid()==17:
  print "[core 17]Got value "+str(recv(0))+" form core 0"
if coreid()==16:
  val=30.0
  print "[core 16]sending value "+str(val)+" to core 2"
  send(val, 2)
if coreid()==2:
  print "[core 2]Got value "+str(recv(16))+" form core 16"
