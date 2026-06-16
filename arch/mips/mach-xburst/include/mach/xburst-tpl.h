/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Shared XBurst1 TPL: per-SoC hook table.
 *
 * The capped XBurst1 SoCs (T10/T20/T21/T30) all boot the same TPL chain - a
 * tiny cache-as-RAM stage that brings up PLL + DDR via the UCLASS_RAM probe,
 * then loads the DRAM-resident SPL from SPI-NOR (cold NOR boot) or returns to
 * the mask ROM (USB boot). The board_init_f() driving that is identical across
 * the four, so it lives once in arch/mips/mach-xburst/tpl.c; the per-SoC pieces
 * - the bare-metal console/SFC helpers (each SoC's register sequence), the
 * console UART, the banner, and the dead-L2 load quirk - are supplied through
 * this hook table, which each SoC defines in its own t<soc>/tpl.c.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __XBURST_TPL_H__
#define __XBURST_TPL_H__

#include <linux/types.h>

struct xburst_tpl_soc {
	/* Bare-metal SPL helpers (per-SoC register sequences). */
	void (*serial_init)(void);
	void (*puts)(const char *s);
	void (*sfc_clk_init)(void);
	void (*nor_read)(unsigned int nor_off, unsigned int *dst,
			 unsigned int bytes);

	unsigned int console_uart;	/* clk_ungate_uart() index */
	unsigned int spl_nor_offs;	/* NOR offset of the DRAM-resident SPL */
	const char *banner;		/* e.g. "\nT20 TPL\n" */

	/*
	 * T30 quirk: its gen-1 ROM left an un-init'd L2 that cannot hold a cached
	 * load of the ~50 KB SPL, so the SPL is read through the uncached window
	 * and not flushed (the SPL switches K0 itself). The other DWC/Innophy
	 * SoCs load cached and flush before jumping.
	 */
	bool uncached_load;

	/*
	 * USB boot (set per-build via CONFIG_SPL_T<soc>_USB_BOOT in the SoC's
	 * ops table): there is no on-flash SPL, so return to the mask ROM after
	 * DDR is up and let the ROM upload U-Boot proper.
	 */
	bool usb_boot;
};

/* Defined by each capped SoC's t<soc>/tpl.c; consumed by mach-xburst/tpl.c. */
extern const struct xburst_tpl_soc xburst_tpl_soc;

/* Common across every XBurst1 SoC's pll.c. */
void clk_ungate_uart(unsigned int idx);

#endif /* __XBURST_TPL_H__ */
