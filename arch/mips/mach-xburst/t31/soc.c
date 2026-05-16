// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 SoC SPL bring-up
 *
 * The SPL runs from ~32 KB of on-chip SRAM with no driver model. It
 * brings up a minimal console, configures the PLLs, inits DDR and then
 * loads U-Boot proper into DRAM. Full U-Boot uses driver model.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t31.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void t31_spl_serial_init(void);
void t31_spl_puts(const char *s);
void __weak sdram_init(void) { }

#ifdef CONFIG_XPL_BUILD
gd_t gdata __section(".bss");

void board_init_f(ulong dummy)
{
	gd = &gdata;
	memset(__bss_start, 0, (size_t)__bss_end - (size_t)__bss_start);

	/*
	 * The mask ROM leaves a usable EXTAL-based clock, so the console
	 * works before pll_init() - bring it up first so any later hang
	 * still produces output.
	 */
	clk_ungate_uart(T31_CONSOLE_UART);
	t31_spl_serial_init();
	t31_spl_puts("\nT31 SPL: alive (pre-PLL)\n");

	pll_init();
	t31_spl_puts("T31 SPL: PLL configured\n");

	sdram_init();

	/*
	 * TODO: load U-Boot proper from SFC and jump to it once DDR init
	 * and the SFC driver land (tasks: DDR Innophy, SFC storage).
	 */
	t31_spl_puts("T31 SPL: DDR/SFC not yet implemented, halting\n");
	for (;;)
		;
}
#endif /* CONFIG_XPL_BUILD */
