#The Extended Epiphany Python for Parallella cluster

This is an Python interpreter used for parallel Python interpreting on Internet connected Epiphanies.
This version is backwards-compatible for all basic communicational calls, the syntax of original parallel Python remains unchanged.
More e-cores can be transparently used with the extended ePython as long as enough Parallellas are connected.

The extended ePython has been developed by Dongyu Liang on the basis of Dr. Nick Brown's original ePython [licenced](LICENCE) under BSD-2.

##Installation

  On master node:

    change the -np argument of mpirun in epython.sh into the number of your parallella node

    Type make

    Then sudo make install followed by starting a new bash session (execute bash at the command line.)

  On slave node(s):

    Type make

    Then sudo make install followed by starting a new bash session (execute bash at the command line.)

##Configuring cluster

    Connect all Parallella boards into a LAN or the Internet

    Add the host name or IP address of Parallella nodes into the .mpi_hostfile in the master node

    Set password less SSH for each host

    Remember to run the ePython in the directory with a .mpi_hostfile in it

##Hello world

  In the shared directory, create a file called hello, then put in the lines
  
    print "Hello world"

  save it, and execute epython hello (or ./epython.sh hello if you have not done make install.)

  Each core within the cluster will display the Hello world message to the screen along with their core id

  For more information about first steps with ePython refer [here](docs/tutorial1.md), for more advanced ePython usage then follow the  tutorials in the [docs directory](docs) which cover writing parallel Python code on the Epiphany.

##Troubleshooting

  Often these are set by default, but if it complains that it can not find e-gcc or the libraries, then you will need to set these  environment variables:

  export PATH=/opt/adapteva/esdk/tools/e-gnu/bin:$PATH
  export EPIPHANY_HOME=/opt/adapteva/esdk

(you might want to place this in your .bashrc file)

##Additional information for installing

MPI is required. This version has been tested with the preinstalled Open MPI of the Parabuntu

If you do not install it then you can still run epython from the current directory, as ./epython.sh but ensure that epython-device.elf is in the current directory when you run the interpreter. The epython.sh script will detect whether to run as sudo (earlier versions of the parallella OS) or not (later versions.)

In order to include files (required for parallel functions) you must either run your Python codes in the same directory as the executables (and the modules directory) and/or export the EPYTHONPATH environment variable to point to the modules directory. When including files, by default ePython will search in the current directory, any subdirectory called modules and then the EPYTHONPATH variable, which follows the same syntax as the PATH variable.

Issuing export export EPYTHONPATH=$EPYTHONPATH:`pwd` in the epython directory will set this to point to the current directory. You can also modify your ~/.bashrc file to contain a similiar command.

For more information about installing ePython refer [here](docs/tutorial1.md), for upgrading ePython refer [here](docs/installupgrade.md)

##Degenerated use

    Pass -d 16 argument to use the single Parallella board (master node)
    
  Notice that Garbage Collector has been disabled.
