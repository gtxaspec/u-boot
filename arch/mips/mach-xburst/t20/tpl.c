// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T20 TPL hook table for the shared cache-as-RAM TPL
 * (arch/mips/mach-xburst/tpl.c). T20 is a DWC SoC with a normal L2, so it loads
 * the DRAM-resident SPL cached and flushes before jumping. The helpers are the
 * vendor SFC/serial transliterations in t20/{serial,sfc}.c.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <mach/t20.h>
#include <mach/xburst-tpl.h>

const struct xburst_tpl_soc xburst_tpl_soc = {
	.serial_init	= t20_spl_serial_init,
	.puts		= t20_spl_puts,
	.sfc_clk_init	= t20_spl_sfc_clk_init,
	.nor_read	= t20_spl_nor_read,
	.console_uart	= T20_CONSOLE_UART,
	.spl_nor_offs	= 0x8000,
	.banner		= "\nT20 TPL\n",
#ifdef CONFIG_SPL_T20_USB_BOOT
	.usb_boot	= true,
#endif
};
