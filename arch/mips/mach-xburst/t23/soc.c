// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T23 SoC SPL bring-up
 *
 * The SPL runs from on-chip SRAM with no driver model: minimal
 * console, PLLs, DDR, then load U-Boot proper. Stage 1 of the T23
 * port brings up console + PLL and (CONFIG_SPL_T23_USB_BOOT)
 * returns to the mask ROM - DDR/SFC are added next. Full U-Boot
 * uses driver model.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t23.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void t23_spl_serial_init(void);
void t23_spl_puts(const char *s);
void t23_spl_putc(char c);
void t23_spl_load_uboot(void);
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
	clk_ungate_uart(T23_CONSOLE_UART);
	t23_spl_serial_init();
	t23_spl_puts("\nT23 SPL: alive (pre-PLL)\n");

	pll_init();
	t23_spl_puts("T23 SPL: PLL configured\n");

	sdram_init();

#ifdef CONFIG_SPL_T23_USB_BOOT
	/*
	 * USB-boot stage1: clocks are up. Return to the mask ROM
	 * (start.S branches in, so $ra still holds the bootrom return
	 * address and this epilogue jr's back into the bootrom USB
	 * loop). The host then uploads U-Boot proper over USB.
	 */
	t23_spl_puts("T23 SPL: returning to mask ROM (USB boot)\n");
	return;
#else
	/* Load U-Boot proper from SPI-NOR into DRAM and jump. */
	t23_spl_puts("T23 SPL: loading U-Boot...\n");
	t23_spl_load_uboot();
	for (;;)
		;
#endif
}
#endif /* CONFIG_XPL_BUILD */
