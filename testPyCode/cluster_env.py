/*
llustration of the cluster evironment testing by showing the Global coreId & nodeId
*/

from parallel import *

print "My global coreid: "+str(coreid())+", I am on node"+str(nodeid())+"."
