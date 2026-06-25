// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 SoC SPL bring-up
 *
 * This SPL is DRAM-resident: the TPL (tpl.c) runs first in the cache-as-RAM
 * window, brings up PLL + DDR (the T32 uMCTL2/Innophy driver ddr_t32.c via the
 * UCLASS_RAM probe), then loads this SPL - from SPI-NOR (NOR cold boot) or the
 * SD card (MSC boot) - into real DRAM and jumps to it. So none of the
 * cache-as-RAM gymnastics a single-stage SPL would need apply here: the flow is
 * fdtdec + the DM scan, the UCLASS_RAM probe records the (already-up) DRAM size,
 * and board_init_r() reads U-Boot proper - from SPI-NOR via the DM SFC driver
 * (SPL_SPI) or from the SD via the DM MMC driver (SPL_MMC) - LZMA-decompresses
 * it and jumps. Full U-Boot uses driver model.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <dm.h>
#include <fdtdec.h>
#include <hang.h>
#include <init.h>
#include <ram.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t32.h>

DECLARE_GLOBAL_DATA_PTR;

static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t32_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t32_spl_putc(hex[(v >> i) & 0xf]);
}

/*
 * Walk a few patterns through DRAM at both an uncached (KSEG1) and a
 * cached (KSEG0) window and verify the read-back. Steps across the
 * full part so a stuck/aliased address line is caught, not just
 * word 0. `size` is the DT-selected variant's DRAM size, from
 * ram_get_info().
 */
static int dram_verify(u32 size)
{
	static const u32 pat[] = {
		0x00000000, 0xffffffff, 0xa5a5a5a5, 0x5a5a5a5a,
		0xdeadbeef, 0x12345678,
	};
	const u32 bases[] = { 0xa0000000, 0x80000000 };
	const u32 offs[] = { 0x0, 0x4, 0x100000, size / 2, size - 4 };
	int b, o, p;

	for (b = 0; b < 2; b++) {
		for (o = 0; o < (int)ARRAY_SIZE(offs); o++) {
			void __iomem *a =
				(void __iomem *)(uintptr_t)(bases[b] + offs[o]);

			for (p = 0; p < (int)ARRAY_SIZE(pat); p++) {
				writel(pat[p], a);
				if (readl(a) != pat[p]) {
					t32_spl_puts("T32 SPL: DDR FAIL @");
					spl_put_hex((u32)(uintptr_t)a);
					t32_spl_puts(" wrote ");
					spl_put_hex(pat[p]);
					t32_spl_puts(" read ");
					spl_put_hex(readl(a));
					t32_spl_putc('\n');
					return -1;
				}
			}
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
	 * The TPL has already brought up PLL + DDR (cache-as-RAM) and loaded
	 * this SPL into real DRAM, then jumped here, so everything runs
	 * DRAM-resident. Bring the console up first so any later hang still
	 * produces output, then fdtdec + the DM scan; the UCLASS_RAM probe
	 * records the (already-up) DRAM size (its bring-up is a no-op in the
	 * SPL phase - the TPL was the first loader stage), and board_init_r()
	 * loads U-Boot proper.
	 */
	clk_ungate_uart(T32_CONSOLE_UART);
	t32_spl_serial_init();

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
	if (!IS_ENABLED(CONFIG_SPL_MMC))
		t32_spl_sfc_clk_init();
	board_init_r(NULL, 0);
	__builtin_unreachable();
}

u32 spl_boot_device(void)
{
	/* MSC/SD cold-boot loads U-Boot from the SD via the SPL MMC path. */
	if (IS_ENABLED(CONFIG_SPL_MMC))
		return BOOT_DEVICE_MMC1;

	return BOOT_DEVICE_SPI;
}
