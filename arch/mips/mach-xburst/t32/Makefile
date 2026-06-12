# SPDX-License-Identifier: GPL-2.0+
#
# T32 SPL: console + PLL (soc.o/pll.o/serial.o), DDR via the
# drivers/ram/ingenic/ddr_t32.c UCLASS_RAM driver probed off the SPL
# devicetree, U-Boot load via the standard SPL_SPI framework (sfc.o
# only pre-sets the SFC clock/controller for the DM SFC driver).
# XBurst1-generic pieces are shared from arch/mips/mach-xburst/.

obj-y	+= soc.o pll.o serial.o sfc.o
