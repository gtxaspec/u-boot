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
void t31_spl_putc(char c);
void t31_spl_load_uboot(void);
void __weak sdram_init(void) { }

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t31_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t31_spl_putc(hex[(v >> i) & 0xf]);
}

/* T31N/L/LC/C100 = 64 MB; T31X/T31AL = 128 MB (M14D1G1664A). */
#if defined(CONFIG_T31_DRAM_128M)
#define T31_DRAM_SIZE	0x08000000u	/* 128 MB */
#else
#define T31_DRAM_SIZE	0x04000000u	/* 64 MB */
#endif

/*
 * Walk a few patterns through DRAM at both an uncached (KSEG1) and a
 * cached (KSEG0) window and verify the read-back. Steps across the
 * full part so a stuck/aliased address line is caught, not just
 * word 0.
 */
static int dram_verify(void)
{
	static const u32 pat[] = {
		0x00000000, 0xffffffff, 0xa5a5a5a5, 0x5a5a5a5a,
		0xdeadbeef, 0x12345678,
	};
	const u32 bases[] = { 0xa0000000, 0x80000000 };
	const u32 offs[] = { 0x0, 0x4, 0x100000,
			     T31_DRAM_SIZE / 2, T31_DRAM_SIZE - 4 };
	int b, o, p;

	for (b = 0; b < 2; b++) {
		for (o = 0; o < (int)ARRAY_SIZE(offs); o++) {
			volatile u32 *a =
				(volatile u32 *)(bases[b] + offs[o]);

			for (p = 0; p < (int)ARRAY_SIZE(pat); p++) {
				*a = pat[p];
				if (*a != pat[p]) {
					t31_spl_puts("T31 SPL: DDR FAIL @");
					spl_put_hex((u32)(uintptr_t)a);
					t31_spl_puts(" wrote ");
					spl_put_hex(pat[p]);
					t31_spl_puts(" read ");
					spl_put_hex(*a);
					t31_spl_putc('\n');
					return -1;
				}
			}
		}
	}
	return 0;
}
#endif

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
	if (dram_verify() == 0)
		t31_spl_puts("T31 SPL: DDR OK\n");

#ifdef CONFIG_SPL_T31_USB_BOOT
	/*
	 * USB-boot stage1: clocks and DDR are up. Return to the mask ROM
	 * (start.S enters with a plain branch, so $ra still holds the
	 * bootrom's return address and this function's epilogue jr's back
	 * into the bootrom USB loop). The host then uploads U-Boot proper
	 * straight into DRAM and issues VR_PROGRAM_START2 - no NOR write.
	 */
	t31_spl_puts("T31 SPL: DDR up; returning to mask ROM (USB boot)\n");
	return;
#else
	/* Load U-Boot proper from SPI-NOR into DRAM and jump (no return). */
	t31_spl_puts("T31 SPL: loading U-Boot...\n");
	t31_spl_load_uboot();
	/* no return */
	for (;;)
		;
#endif
}
#endif /* CONFIG_XPL_BUILD */
