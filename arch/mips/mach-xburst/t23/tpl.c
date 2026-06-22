// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T23 TPL hook table for the shared cache-as-RAM TPL
 * (arch/mips/mach-xburst/tpl.c). T23 is an Innophy SoC with a normal L2, so it
 * loads the DRAM-resident SPL cached and flushes before jumping. The helpers
 * are the vendor SFC/serial transliterations in t23/{serial,sfc}.c.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <mach/t23.h>
#include <mach/xburst-tpl.h>

const struct xburst_tpl_soc xburst_tpl_soc = {
	.serial_init	= t23_spl_serial_init,
	.puts		= t23_spl_puts,
	.sfc_clk_init	= t23_spl_sfc_clk_init,
	.nor_read	= t23_spl_nor_read,
	.console_uart	= T23_CONSOLE_UART,
	.spl_nor_offs	= 0x8000,
	.banner		= "\nT23 TPL\n",
	.usb_boot	= IS_ENABLED(CONFIG_SPL_T23_USB_BOOT),
#if IS_ENABLED(CONFIG_SPL_MMC)
	.msc_read	= t23_tpl_msc_read,
	.spl_msc_skip	= 0x10000,	/* SD byte offset of the SPL image */
#endif
};
