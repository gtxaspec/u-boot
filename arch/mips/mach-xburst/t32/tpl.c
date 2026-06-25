// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 TPL hook table for the shared cache-as-RAM TPL
 * (arch/mips/mach-xburst/tpl.c). T32 pairs a Synopsys uMCTL2 controller with an
 * Innophy training PHY. The DRAM-resident SPL plus its appended dtb is larger
 * than the 32 KB L1, so it is loaded UNCACHED (KSEG1, straight to DRAM): a cached
 * load while the TPL is still in cache-as-RAM does not survive L1 eviction, and
 * the appended dtb reads back as garbage (fdtdec then fails). The helpers are the
 * vendor SFC2/serial transliterations in t32/{serial,sfc}.c.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <mach/t32.h>
#include <mach/xburst-tpl.h>

const struct xburst_tpl_soc xburst_tpl_soc = {
	.serial_init	= t32_spl_serial_init,
	.puts		= t32_spl_puts,
	.sfc_clk_init	= t32_spl_sfc_clk_init,
	.nor_read	= t32_spl_nor_read,
	.uncached_load	= true,
	.console_uart	= T32_CONSOLE_UART,
	.spl_nor_offs	= 0x8000,
	.banner		= "\nT32 TPL\n",
	.usb_boot	= IS_ENABLED(CONFIG_SPL_T32_USB_BOOT),
#if IS_ENABLED(CONFIG_SPL_MMC)
	.msc_read	= xburst_tpl_msc_read,
	.msc_base	= 0xb3060000,	/* T32 MSC0 (vs 0xb3450000 on the T31-class) */
	.spl_msc_skip	= 0x10000,	/* SD byte offset of the SPL image */
#endif
};
