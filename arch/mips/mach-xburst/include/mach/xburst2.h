/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Helpers shared by the XBurst2 SoCs (T40, T41, A1)
 */

#ifndef __MACH_XBURST2_H
#define __MACH_XBURST2_H

#ifdef CONFIG_XBURST2_L2_128K
void xburst2_set_l2_128k(void);
#else
static inline void xburst2_set_l2_128k(void) {}
#endif

#endif /* __MACH_XBURST2_H */
