# SPDX-License-Identifier: GPL-2.0+
#
# T23 console + PLL + SFC; DDR is the DT-driven shared Innophy UCLASS_RAM driver
# in drivers/ram/ingenic/ddr_t31.c. XBurst1-generic pieces are shared from
# arch/mips/mach-xburst/.
#
# Phase split (mt7621 pattern): the TPL (cache-as-RAM) brings up PLL+DDR and
# loads the DRAM-resident SPL; the SPL (board_init_f in soc.c) runs the DM scan
# and loads U-Boot. The shared low-level helpers are built for both loader
# phases (TPL + SPL).

obj-$(CONFIG_XPL_BUILD)	+= pll.o serial.o sfc.o

ifeq ($(CONFIG_TPL_BUILD),y)
obj-y	+= tpl.o
else
obj-$(CONFIG_XPL_BUILD)	+= soc.o
endif
