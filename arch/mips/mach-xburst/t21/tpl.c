// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T21 TPL hook table for the shared cache-as-RAM TPL
 * (arch/mips/mach-xburst/tpl.c). T21 is an Innophy SoC with a normal L2, so it
 * loads the DRAM-resident SPL cached and flushes before jumping. The helpers
 * are the vendor SFC/serial transliterations in t21/{serial,sfc}.c.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <mach/t21.h>
#include <mach/xburst-tpl.h>

const struct xburst_tpl_soc xburst_tpl_soc = {
	.serial_init	= t21_spl_serial_init,
	.puts		= t21_spl_puts,
	.sfc_clk_init	= t21_spl_sfc_clk_init,
	.nor_read	= t21_spl_nor_read,
	.console_uart	= T21_CONSOLE_UART,
	.spl_nor_offs	= 0x8000,
	.banner		= "\nT21 TPL\n",
#ifdef CONFIG_SPL_T21_USB_BOOT
	.usb_boot	= true,
#endif
};
