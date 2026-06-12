# SPDX-License-Identifier: GPL-2.0+
#
# T23 SPL: console + PLL (soc.o/pll.o/serial.o), DDR via the shared
# drivers/ram/ingenic/ddr_t31.c UCLASS_RAM driver (T23 variant) probed
# off the SPL devicetree, U-Boot load via the standard SPL_SPI
# framework (sfc.o only pre-sets the SFC clock/controller for the DM
# SFC driver). XBurst1-generic pieces are shared from
# arch/mips/mach-xburst/.

obj-y	+= soc.o pll.o serial.o sfc.o
