// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T30 TPL hook table for the shared cache-as-RAM TPL
 * (arch/mips/mach-xburst/tpl.c). T30 is an Innophy SoC, but its gen-1 ROM left
 * an un-init'd L2 that cannot hold a cached load of the ~50 KB SPL, so it reads
 * the SPL through the uncached window and does not flush (uncached_load = true;
 * the SPL switches K0 itself - see t30/soc.c). The helpers are the vendor
 * SFC/serial transliterations in t30/{serial,sfc}.c.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <mach/t30.h>
#include <mach/xburst-tpl.h>

const struct xburst_tpl_soc xburst_tpl_soc = {
	.serial_init	= t30_spl_serial_init,
	.puts		= t30_spl_puts,
	.sfc_clk_init	= t30_spl_sfc_clk_init,
	.nor_read	= t30_spl_nor_read,
	.console_uart	= T30_CONSOLE_UART,
	.spl_nor_offs	= 0x8000,
	.banner		= "\nT30 TPL\n",
	.uncached_load	= true,
#ifdef CONFIG_SPL_T30_USB_BOOT
	.usb_boot	= true,
#endif
};
