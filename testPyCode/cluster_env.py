/*
llustration of the environmental native functions by printing
the Global coreId & nodeId for all cores
*/

from parallel import *

print "My global coreid: "+str(coreid())+", I am on node"+str(nodeid())+"."

if coreid()==(numcores()-1):
    print "The cluster has "+str(numcores())+" cores in "+str(numnodes())+" node(s)."
