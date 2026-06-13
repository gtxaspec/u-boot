# SPDX-License-Identifier: GPL-2.0+
#
# T21 SPL: console + PLL (soc.o/pll.o/serial.o), DDR via the shared
# drivers/ram/ingenic/ddr_t31.c driver (T21 variant, imperative call +
# UCLASS_RAM probe), U-Boot load via the standard SPL_SPI framework
# (sfc.o pre-sets the SFC clock/controller for the DM SFC driver and
# provides the polled NOR read for the cache-as-RAM reload).
# XBurst1-generic pieces are shared from arch/mips/mach-xburst/.

obj-y	+= soc.o pll.o serial.o sfc.o
