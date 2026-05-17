// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T23 SoC SPL bring-up
 *
 * The SPL runs from on-chip SRAM with no driver model: minimal
 * console, PLLs, Innophy DDR2, then load U-Boot proper. With
 * CONFIG_SPL_T23_USB_BOOT it brings up console + PLL + DDR and
 * returns to the mask ROM (SFC NOR-boot lands next). Full U-Boot
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
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t23_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t23_spl_putc(hex[(v >> i) & 0xf]);
}

#if defined(CONFIG_T23_DRAM_32M)
#define T23_DRAM_SIZE	0x02000000u	/* 32 MB */
#else
#define T23_DRAM_SIZE	0x04000000u	/* 64 MB */
#endif

/*
 * Verify DRAM up to the *configured* size (T23 is 64 MB, or 32 MB
 * for T23DL/DN - never 128 MB). Two passes:
 *
 *  1. Stuck-bit: walk patterns at offsets within [0, SIZE) at both
 *     the KSEG1 (uncached) and KSEG0 (cached) windows.
 *  2. Alias: write a unique marker per offset across [0, SIZE) via
 *     KSEG1, then read them all back. If the DDR controller is
 *     configured larger than the populated part (the classic
 *     wrong-geometry bug) the high offsets wrap onto the low ones
 *     and the markers collide - so "DDR OK" actually proves the
 *     size/geometry, instead of a write-then-read-same-address
 *     test that silently passes on an aliased part.
 */
static int dram_verify(void)
{
	static const u32 pat[] = {
		0x00000000, 0xffffffff, 0xa5a5a5a5, 0x5a5a5a5a,
		0xdeadbeef, 0x12345678,
	};
	const u32 offs[] = {
		0x0, 0x4, 0x100000, T23_DRAM_SIZE / 4,
		T23_DRAM_SIZE / 2, T23_DRAM_SIZE - 4,
	};
	const u32 bases[] = { 0xa0000000, 0x80000000 };
	int b, o, p;

	for (b = 0; b < 2; b++) {
		for (o = 0; o < (int)ARRAY_SIZE(offs); o++) {
			volatile u32 *a =
				(volatile u32 *)(bases[b] + offs[o]);

			for (p = 0; p < (int)ARRAY_SIZE(pat); p++) {
				*a = pat[p];
				if (*a != pat[p]) {
					t23_spl_puts("T23 SPL: DDR FAIL @");
					spl_put_hex((u32)(uintptr_t)a);
					t23_spl_puts(" wrote ");
					spl_put_hex(pat[p]);
					t23_spl_puts(" read ");
					spl_put_hex(*a);
					t23_spl_putc('\n');
					return -1;
				}
			}
		}
	}

	for (o = 0; o < (int)ARRAY_SIZE(offs); o++)
		*(volatile u32 *)(0xa0000000 + offs[o]) = 0xa5000000 | offs[o];
	for (o = 0; o < (int)ARRAY_SIZE(offs); o++) {
		volatile u32 *a = (volatile u32 *)(0xa0000000 + offs[o]);

		if (*a != (0xa5000000 | offs[o])) {
			t23_spl_puts("T23 SPL: DDR ALIAS @");
			spl_put_hex((u32)(uintptr_t)a);
			t23_spl_puts(" read ");
			spl_put_hex(*a);
			t23_spl_puts(" (controller mis-sized vs part)\n");
			return -1;
		}
	}
	return 0;
}

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
	if (dram_verify() == 0) {
		t23_spl_puts("T23 SPL: DDR OK ");
		spl_put_hex(T23_DRAM_SIZE);
		t23_spl_puts(" (alias-checked)\n");
	}

#ifdef CONFIG_SPL_T23_USB_BOOT
	/*
	 * USB-boot stage1: clocks and DDR are up. Return to the mask
	 * ROM (start.S branches in, so $ra still holds the bootrom
	 * return address and this epilogue jr's back into the bootrom
	 * USB loop). The host then uploads U-Boot proper over USB.
	 */
	t23_spl_puts("T23 SPL: DDR up; returning to mask ROM (USB boot)\n");
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
