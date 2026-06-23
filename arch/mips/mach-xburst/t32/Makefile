# SPDX-License-Identifier: GPL-2.0+
#
# T32 boots the shared XBurst1 TPL chain: a cache-as-RAM TPL (tpl.o + the
# shared arch/mips/mach-xburst/tpl.o) brings up PLL + DDR via the
# drivers/ram/ingenic/ddr_t32.c UCLASS_RAM probe and loads the DRAM-resident
# SPL (soc.o) off NOR (sfc.o) or SD; the SPL then loads U-Boot proper through
# the standard SPL_SPI / SPL_MMC framework. pll/serial/sfc are shared by both
# stages; XBurst1-generic pieces come from arch/mips/mach-xburst/.

obj-$(CONFIG_XPL_BUILD)	+= pll.o serial.o sfc.o
ifeq ($(CONFIG_TPL_BUILD),y)
obj-y	+= tpl.o
else
obj-$(CONFIG_XPL_BUILD)	+= soc.o
endif
