// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T10 TPL hook table for the shared cache-as-RAM TPL
 * (arch/mips/mach-xburst/tpl.c). T10 is a DWC SoC with a normal L2, so it loads
 * the DRAM-resident SPL cached and flushes before jumping. The helpers are the
 * vendor SFC/serial transliterations in t10/{serial,sfc}.c.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <mach/t10.h>
#include <mach/xburst-tpl.h>

const struct xburst_tpl_soc xburst_tpl_soc = {
	.serial_init	= t10_spl_serial_init,
	.puts		= t10_spl_puts,
	.sfc_clk_init	= t10_spl_sfc_clk_init,
	.nor_read	= t10_spl_nor_read,
	.console_uart	= T10_CONSOLE_UART,
	.spl_nor_offs	= 0x8000,
	.banner		= "\nT10 TPL\n",
#ifdef CONFIG_SPL_T10_USB_BOOT
	.usb_boot	= true,
#endif
};
