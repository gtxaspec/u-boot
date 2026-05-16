// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 SoC SPL bring-up
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <init.h>
#include <hang.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t31.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void enable_caches(void);
void __weak sdram_init(void) { }

#ifdef CONFIG_XPL_BUILD
gd_t gdata __section(".bss");

void board_init_f(ulong dummy)
{
	gd = &gdata;
	memset(__bss_start, 0, (size_t)__bss_end - (size_t)__bss_start);

	/*
	 * The mask ROM leaves a usable system clock, so bring the console
	 * up before touching the PLLs: any later hang still produces output.
	 * spl_early_init() sets up the FDT and driver model so the DM
	 * ns16550 can bind (and set the Ingenic UART module-enable bit).
	 */
	clk_ungate_uart(T31_CONSOLE_UART);

	if (spl_early_init())
		hang();

	preloader_console_init();
	puts("T31 SPL: console up (pre-PLL)\n");

	pll_init();
	puts("T31 SPL: PLL configured\n");

	sdram_init();

	enable_caches();

	gd->flags |= GD_FLG_SPL_INIT;

	/*
	 * TODO: load U-Boot proper from SFC and jump to it once the SFC
	 * driver and DDR init land (tasks: DDR Innophy, SFC storage).
	 */
	puts("T31 SPL: DDR/SFC not yet implemented, halting\n");
	hang();
}
#endif /* CONFIG_XPL_BUILD */
