// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T21 SoC SPL bring-up
 *
 * The SPL runs from on-chip SRAM with no driver model: minimal
 * console, PLLs, Innophy DDR2, then load U-Boot proper. With
 * CONFIG_SPL_T21_USB_BOOT it brings up console + PLL + DDR and
 * returns to the mask ROM (SFC NOR-boot lands next). Full U-Boot
 * uses driver model.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t21.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void t21_spl_serial_init(void);
void t21_spl_puts(const char *s);
void t21_spl_putc(char c);
void t21_spl_load_uboot(void);
void __weak sdram_init(void) { }

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t21_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t21_spl_putc(hex[(v >> i) & 0xf]);
}

/* T21N is 64 MB (M14D5121632A); no 128/32 MB T21 board in scope. */
#define T21_DRAM_SIZE	0x04000000u	/* 64 MB */

/*
 * Verify DRAM up to the configured size. Two passes: a stuck-bit
 * pattern walk at both the KSEG1 (uncached) and KSEG0 (cached)
 * windows, then an alias pass (unique marker per offset across
 * [0, SIZE), all read back) so "DDR OK" actually proves the
 * geometry/size rather than passing on an aliased part.
 */
static int dram_verify(void)
{
	static const u32 pat[] = {
		0x00000000, 0xffffffff, 0xa5a5a5a5, 0x5a5a5a5a,
		0xdeadbeef, 0x12345678,
	};
	const u32 offs[] = {
		0x0, 0x4, 0x100000, T21_DRAM_SIZE / 4,
		T21_DRAM_SIZE / 2, T21_DRAM_SIZE - 4,
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
					t21_spl_puts("T21 SPL: DDR FAIL @");
					spl_put_hex((u32)(uintptr_t)a);
					t21_spl_puts(" wrote ");
					spl_put_hex(pat[p]);
					t21_spl_puts(" read ");
					spl_put_hex(*a);
					t21_spl_putc('\n');
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
			t21_spl_puts("T21 SPL: DDR ALIAS @");
			spl_put_hex((u32)(uintptr_t)a);
			t21_spl_puts(" read ");
			spl_put_hex(*a);
			t21_spl_puts(" (controller mis-sized vs part)\n");
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
	clk_ungate_uart(T21_CONSOLE_UART);
	t21_spl_serial_init();
	t21_spl_puts("\nT21 SPL: alive (pre-PLL)\n");

	pll_init();
	t21_spl_puts("T21 SPL: PLL configured\n");

	sdram_init();
	if (dram_verify() == 0) {
		t21_spl_puts("T21 SPL: DDR OK ");
		spl_put_hex(T21_DRAM_SIZE);
		t21_spl_puts(" (alias-checked)\n");
	}

#ifdef CONFIG_SPL_T21_USB_BOOT
	/*
	 * USB-boot stage1: clocks and DDR are up. Return to the mask
	 * ROM (start.S branches in, so $ra still holds the bootrom
	 * return address and this epilogue jr's back into the bootrom
	 * USB loop). The host then uploads U-Boot proper over USB.
	 */
	t21_spl_puts("T21 SPL: DDR up; returning to mask ROM (USB boot)\n");
	return;
#else
	/* Load U-Boot proper from SPI-NOR into DRAM and jump. */
	t21_spl_puts("T21 SPL: loading U-Boot...\n");
	t21_spl_load_uboot();
	for (;;)
		;
#endif
}
#endif /* CONFIG_XPL_BUILD */
