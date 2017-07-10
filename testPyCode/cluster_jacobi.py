/*
Jacobi iteration to solve Laplace's equation for diffusion in one dimension
This illustrates distributing data amongst the cluster cores, halo swapping
*/

from parallel import *
from math import sqrt

DATA_SIZE=1000
MAX_ITS=10000

# Work out the amount of data to hold on this core
local_size=DATA_SIZE/numcores()
if local_size * numcores() != DATA_SIZE:
        if (coreid() < DATA_SIZE-local_size*numcores()): local_size=local_size+1

# Allocate the two arrays (two as this is Jacobi) we +2 to account for halos/boundary conditions
data=[0] * (local_size+2)
data_p1=[0]* (local_size+2)

# Set the initial conditions
i=0
while i<=local_size+1:
        data[i]=0.0
        i+=1

if coreid()==0: data[0]=1.0
if coreid()==numcores()-1: data[local_size+1]=10.0


its=0
while its < MAX_ITS:
        # Halo swap to my left and right neighbours if I have them
        if (coreid() > 0): data[0]=sendrecv(data[1], coreid()-1)
        if (coreid() < numcores()-1): data[local_size+1]=sendrecv(data[local_size], coreid()+1)


        if coreid()==0 and its%1000 == 0: print "program is at "+its+" iterations"

        # Performs the Jacobi iteration for Laplace
        i=1
        while i<=local_size:
                data_p1[i]=0.5* (data[i-1] + data[i+1])
                i+=1
        # Swap data around for next iteration
        i=1
        while i<=local_size:
                data[i]=data_p1[i]
                i+=1
        its+=1

if coreid()==0: print "Completed in "+str(its)+" iterations"
