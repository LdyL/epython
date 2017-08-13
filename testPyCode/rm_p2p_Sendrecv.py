/*
Illustration of P2P blocking sendrecv between core 0 to core 17.
*/

from parallel import *

n=1

data=[0]*2
i=0
if coreid()==0:
    data[0]=0.0
    data[1]=0.0
    print "Core"+str(coreid())+" has value: "+"["+str(data[0])+","+str(data[1])+"]"
    while i<n:
        data[1]=sendrecv(data[1],17)
        i+=1
    print "Core"+str(coreid())+" has value: "+"["+str(data[0])+","+str(data[1])+"]"
if coreid()==17:
    data[0]=1.0
    data[1]=1.0
    print "Core"+str(coreid())+" has value: "+"["+str(data[0])+","+str(data[1])+"]"
    while i<n:
        data[1]=sendrecv(data[1],0)
        i+=1
    print "Core"+str(coreid())+" has value: "+"["+str(data[0])+","+str(data[1])+"]"
