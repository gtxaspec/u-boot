// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T20 SoC SPL bring-up
 *
 * The SPL runs from on-chip SRAM with no driver model: minimal
 * console, PLLs, DDR, then load U-Boot proper. Stage 1 brings up
 * console + PLL and (CONFIG_SPL_T20_USB_BOOT) returns to the mask
 * ROM; the DWC DDR2 (Stage 2) + SFC NOR-boot (Stage 3) land next.
 * Full U-Boot uses driver model.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t20.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void t20_spl_serial_init(void);
void t20_spl_puts(const char *s);
void t20_spl_putc(char c);
void t20_spl_load_uboot(void);
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
	clk_ungate_uart(T20_CONSOLE_UART);
	t20_spl_serial_init();
	t20_spl_puts("\nT20 SPL: alive (pre-PLL)\n");

	pll_init();
	t20_spl_puts("T20 SPL: PLL configured\n");

	sdram_init();

#ifdef CONFIG_SPL_T20_USB_BOOT
	/*
	 * USB-boot stage1: clocks are up. Return to the mask ROM
	 * (start.S branches in, so $ra still holds the bootrom return
	 * address and this epilogue jr's back into the bootrom USB
	 * loop). The host then uploads U-Boot proper over USB.
	 */
	t20_spl_puts("T20 SPL: returning to mask ROM (USB boot)\n");
	return;
#else
	/* Load U-Boot proper from SPI-NOR into DRAM and jump. */
	t20_spl_puts("T20 SPL: loading U-Boot...\n");
	t20_spl_load_uboot();
	for (;;)
		;
#endif
}
#endif /* CONFIG_XPL_BUILD */
