# SPDX-License-Identifier: GPL-2.0+
#
# T10 console + PLL + SFC; DDR is the DT-driven shared Synopsys-DWC UCLASS_RAM
# driver in drivers/ram/ingenic/ddr_t20.c (shared with T20). XBurst1-generic
# pieces are shared from arch/mips/mach-xburst/.
#
# Phase split (mt7621 pattern): the TPL (cache-as-RAM) brings up PLL+DDR and
# loads the DRAM-resident SPL; the SPL (board_init_f in soc.c) runs the DM scan
# and loads U-Boot. Shared low-level helpers are built for every phase.

obj-y	+= pll.o serial.o sfc.o

ifeq ($(CONFIG_TPL_BUILD),y)
obj-y	+= tpl.o
else
obj-y	+= soc.o
endif
