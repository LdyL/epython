/*
Illustration of cross P2P blocking message passing from core 1 to core 18
and from core 17 to core 2.
*/

from parallel import *

n=1

i=0
if coreid()==1:
  val=20
  print "[core 1]sending value "+str(val)+" to core 18"
  while i<n:
    send(val, 18)
    i+=1
if coreid()==18:
  val=0
  while i<n:
    val=recv(1)
    i+=1
  print "[core 18]Got value "+str(val)+" from core 1"
if coreid()==17:
  val=30.0
  print "[core 17]sending value "+str(val)+" to core 2"
  while i<n:
    send(val, 2)
    i+=1
if coreid()==2:
  val=0
  while i<n:
    val=recv(17)
    i+=1
  print "[core 2]Got value "+str(val)+" from core 17"
