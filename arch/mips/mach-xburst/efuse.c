// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst per-die eFUSE chip serial. The 128-bit serial sits at a fixed
 * address that is the same across the T-series; both the Ethernet MAC
 * derivation (ethaddr.c) and the DFU gadget's USB serial number (dfu.c) read
 * it, so the access lives here once.
 */

#include <asm/io.h>
#include <mach/efuse.h>
#include <linux/types.h>

/* eFUSE chip-serial words, uncached KSEG1 (physical 0x13540200..). */
#define XBURST_EFUSE_SERIAL0	0xb3540200
#define XBURST_EFUSE_SERIAL1	0xb3540204
#define XBURST_EFUSE_SERIAL2	0xb3540208
#define XBURST_EFUSE_SERIAL3	0xb354023c

void xburst_chip_serial(u32 s[4])
{
	s[0] = readl((void __iomem *)XBURST_EFUSE_SERIAL0);
	s[1] = readl((void __iomem *)XBURST_EFUSE_SERIAL1);
	s[2] = readl((void __iomem *)XBURST_EFUSE_SERIAL2);
	s[3] = readl((void __iomem *)XBURST_EFUSE_SERIAL3);
}
