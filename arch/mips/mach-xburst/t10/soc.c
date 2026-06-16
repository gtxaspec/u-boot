// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T10 SoC SPL bring-up
 *
 * This SPL is DRAM-resident: the TPL (tpl.c) runs first in the 32 KB L1
 * cache-as-RAM window, brings up PLL + DDR (the shared Synopsys-DWC driver
 * ddr_t20.c via the UCLASS_RAM probe), then loads this SPL from SPI-NOR into
 * real DRAM and jumps to it. So none of the cache-as-RAM gymnastics the old
 * non-DM single-stage SPL needed (imperative pre-DM DDR init, the hand-rolled
 * LZMA U-Boot loader) apply here - the flow is the plain T20/T21/T23/T31 one:
 * fdtdec + the DM scan, the UCLASS_RAM probe records the (already-up) DRAM
 * size, and board_init_r() reads U-Boot proper from SPI-NOR via the DM SFC
 * driver, LZMA-decompresses it and jumps.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <cpu_func.h>
#include <dm.h>
#include <fdtdec.h>
#include <hang.h>
#include <init.h>
#include <ram.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t10.h>

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t10_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t10_spl_putc(hex[(v >> i) & 0xf]);
}

/*
 * Verify DRAM up to the DT-selected SKU size (T10 is 64 MB). Two passes:
 *
 *  1. Stuck-bit: walk patterns at offsets within [0, size) at both the
 *     KSEG1 (uncached) and KSEG0 (cached) windows.
 *  2. Alias: write a unique marker per offset across [0, size) via KSEG1,
 *     then read them all back. If the DDR controller is configured larger
 *     than the populated part (the classic wrong-geometry bug) the high
 *     offsets wrap onto the low ones and the markers collide - so "DDR OK"
 *     actually proves the size/geometry.
 */
static int dram_verify(u32 size)
{
	static const u32 pat[] = {
		0x00000000, 0xffffffff, 0xa5a5a5a5, 0x5a5a5a5a,
		0xdeadbeef, 0x12345678,
	};
	const u32 offs[] = {
		0x0, 0x4, 0x100000, size / 4, size / 2, size - 4,
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
					t10_spl_puts("T10 SPL: DDR FAIL @");
					spl_put_hex((u32)(uintptr_t)a);
					t10_spl_puts(" wrote ");
					spl_put_hex(pat[p]);
					t10_spl_puts(" read ");
					spl_put_hex(*a);
					t10_spl_putc('\n');
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
			t10_spl_puts("T10 SPL: DDR ALIAS @");
			spl_put_hex((u32)(uintptr_t)a);
			t10_spl_puts(" read ");
			spl_put_hex(*a);
			t10_spl_puts(" (controller mis-sized vs part)\n");
			return -1;
		}
	}
	return 0;
}

gd_t gdata __section(".bss");

void board_init_f(ulong dummy)
{
	struct udevice *dev;
	struct ram_info ram;

	/*
	 * The TPL has already brought up PLL + DDR (cache-as-RAM) and loaded this
	 * SPL into real DRAM, then jumped here, so everything runs DRAM-resident:
	 * no cache-as-RAM staging, no hand-rolled loader. The flow now matches
	 * T20/T21/T23/T31 - fdtdec + the DM scan, the UCLASS_RAM probe records the
	 * (already-up) DRAM size, and board_init_r() reads U-Boot proper from
	 * SPI-NOR via the DM SFC driver, LZMA-decompresses it and jumps.
	 */
	clk_ungate_uart(T10_CONSOLE_UART);
	t10_spl_serial_init();

	memset(__bss_start, 0, (size_t)__bss_end - (size_t)__bss_start);
	gd = &gdata;

	if (fdtdec_setup())
		hang();
	if (spl_init())
		hang();
	if (uclass_first_device_err(UCLASS_RAM, &dev))
		hang();
	if (ram_get_info(dev, &ram))
		hang();
	dram_verify((u32)ram.size);

	preloader_console_init();
	t10_spl_sfc_clk_init();
	board_init_r(NULL, 0);
	__builtin_unreachable();
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_SPI;
}
#endif /* CONFIG_XPL_BUILD */
