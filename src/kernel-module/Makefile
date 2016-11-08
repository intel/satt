#
# Processor trace & sideband logger for Software Analysis Trace Tool
# Copyright (c) 2014, Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.

# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.

# -------------------- user configurable options ------------------------

# base name of SAT driver
MODULE_NAME := sat

# location to install driver
#INSTALL = ../sat

EXTRA_CFLAGS += -I$(src) -I$(obj)/ -I$(srctree)/arch/x86/lib/
EXTRA_CFLAGS += -I$(srctree)/arch/x86/include/
EXTRA_CFLAGS += -I$(srctree)/arch/x86/include/asm/

SMP=SMP

obj-m := sat.o

sat-objs := \
	main.o \
	sideband_tracer.o \
	sat-tracer.o \
	sat-buffer.o \
	multi-buffer.o \
	sat-panic.o \
	power-mon.o \
	schedule_wrapper.o \
	external/symbols.o \
	external/kernfunc.o \
	external/hijacks.o


#schedule_wrapper.o: schedule_wrapper.S
#	nasm -f -elf64 schedule_wrapper.S

all:
	$(MAKE) -C $1 M=$(PWD) modules

clean:
	rm -f sat.ko.st* .sat.ko.stripped.*
	rm -f *.o .*.o.cmd .*.o.d .*.ko.cmd .*.ko.unsigned.cmd *.gcno
	rm -f $(MODULE_NAME).ko $(MODULE_NAME).ko.unsigned
	rm -f $(DRIVER_MODE)*$(DRIVER_TYPE).ko
	rm -f Module.symvers Modules.symvers *.mod.c modules.order Module.markers
	rm -rf .tmp_versions

	rm -f external/*.o external/.*.o.cmd external/.*.o.d external/.*.ko.cmd external/.*.ko.unsigned.cmd external/*.gcno
	rm -f external/*.ko external/$(MODULE_NAME).ko.unsigned
	rm -f external/Module.symvers external/Modules.symvers external/*.mod.c external/modules.order external/Module.markers
