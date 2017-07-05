/*
llustration of the cluster evironment testing by showing the Global coreId & nodeId
*/

from parallel import *

if coreid()==0:
    print "The cluster has "+str(numcores())+" cores in "+str(numnodes())+" node(s)."

print "My global coreid: "+str(coreid())+", I am on node"+str(nodeid())+"."
