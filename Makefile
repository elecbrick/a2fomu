#
# Makefile - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
#
# This file is part of a2fomu which is released under the two clause BSD
# licence.  See file LICENSE in the project root directory or visit the
# project at https://github.com/elecbrick/a2fomu for full license details.

# Project root Makefile
#
# Type the command "make" in the project root directory and the following steps
# will be taken:
#
# 1. Create gateware logic, map registers and generate header files that the
#    software can use to configure and interact with the hardware.
#
# 2. Create software binaries for the runtime as well as second and third stage
#    loaders plus any libraries that are used.
#
# 3. Generate initial filesystem to be loaded on an empty Fomu.
#
# 4. Generate documentation for the hardware interface.
#
# 5. Bundle the gateware, loader, runtime and filesystem into a single DFU
#    image.
#
# 6. Write the image of step 4 into a Fomu that is in DFU mode (default).
#
# 7. Open a terminal session to the a2fomu device.

all:
	cd hw && $(MAKE) build/gateware/top.txt
	cd sw/src && $(MAKE) all
	cd hw && $(MAKE) flash
