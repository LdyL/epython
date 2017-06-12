/*
Illustration of P2P blocking send from core 0 to core 17.
*/

from parallel import *

if coreid()==0:
  send(20, 17)
