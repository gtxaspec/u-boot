/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic XBurst per-die eFUSE chip serial.
 */

#ifndef __MACH_XBURST_EFUSE_H__
#define __MACH_XBURST_EFUSE_H__

#include <linux/types.h>

/*
 * Read the SoC's 128-bit per-die eFUSE chip serial into s[0..3]. The serial
 * sits at the same fixed address across the XBurst T-series.
 */
void xburst_chip_serial(u32 s[4]);

#endif /* __MACH_XBURST_EFUSE_H__ */
