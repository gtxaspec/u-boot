# SPDX-License-Identifier: GPL-2.0+
#
# T33 SPL: stage 1 = console + PLL, stage 2 = uMCTL2/Innophy DDR2
# (sdram.o), forward-ported from the vendor U-Boot 2022.10 PRJ
# template (PRJ008 = T33). The SFC NOR loader (sfc.o) lands with
# stage 3. XBurst1-generic pieces are shared from
# arch/mips/mach-xburst/.

obj-y	+= soc.o pll.o serial.o sdram.o
