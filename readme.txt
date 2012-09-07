# topdown #

This directory contains the source for the topdown.sys device driver.  It can
be compiled via the command line using the WDK or with Visual Studio 2012.

The topdown.sys driver should not be run on a production system.  It has
an API that allows arbitrary writes into kernel space which could allow
an unprivileged user arbitrary control of the machine.  This driver is
for test systems only.

# TopDownControl #

This directory contains the VS2010 control program.  It allows you to enable
and disable listening for new processes, along with adding and removing which
processes to listen for.

# ShowMemory #

This is a simple VS2010 test program that allocates memory, truncates it to
a 32-bit value, and prints it out.  This is used to make sure the MEM_TOP_DOWN 
flag is being set.
