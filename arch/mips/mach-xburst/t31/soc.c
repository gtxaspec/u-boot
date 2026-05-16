// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 SoC SPL bring-up
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <init.h>
#include <hang.h>
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

	pll_init();
	clk_ungate_uart(T31_CONSOLE_UART);

	sdram_init();

	enable_caches();

	memset(__bss_start, 0, (size_t)__bss_end - (size_t)__bss_start);

	gd->flags |= GD_FLG_SPL_INIT;

	/*
	 * TODO: bring up the serial console, then load U-Boot proper from
	 * SFC and jump to it once the SFC driver and DDR init land
	 * (tasks: serial console, DDR Innophy, SFC storage).
	 */
	hang();
}
#endif /* CONFIG_XPL_BUILD */
