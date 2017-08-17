#!/bin/bash

set -e

ELIBS="${EPIPHANY_HOME}/tools/host/lib"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${ELIBS}
EHDF=/opt/adapteva/esdk/bsps/current/parallella_E16G3_1GB.hdf

export EPYTHONPATH=$EPYTHONPATH:`pwd`

OS_MAJ="$(uname -a | cut -d' ' -f3 | cut -d'.' -f1)"
OS_VER="$(uname -a | cut -d' ' -f3 | cut -d'.' -f2)"

FILE=epython-host

if [ -f $FILE ]
then
FILE=./epython-host
else
FILE=/usr/bin/epython-host
fi

if [[ "$OS_VER" -ge "14" || "$OS_MAJ" -gt "3" ]]
then
mpirun -x LD_LIBRARY_PATH -x EPIPHANY_HDF=${EHDF} -x EPYTHONPATH -np 2 --hostfile .mpi_hostfile $FILE "$@"
else
echo "ERROR - Unsupported hardware"
fi
