// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 SoC SPL bring-up
 *
 * The SPL runs from on-chip SRAM (TCSM, measured >=128 KB via the
 * bootrom dense read/write probe). It brings up a minimal console, then
 * driver model, whose UCLASS_RAM probe sets the PLLs and brings DDR up from
 * the per-SKU "ingenic,sdram-params" DT array (T31 is the uncapped SoC, so
 * this single SPL is the first loader stage). It then hands U-Boot loading to
 * the standard SPL_SPI framework: board_init_r() reads U-Boot proper from
 * SPI-NOR via the DM SFC driver, LZMA-decompresses it and jumps. Full U-Boot
 * uses driver model.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <dm.h>
#include <fdtdec.h>
#include <hang.h>
#include <init.h>
#include <ram.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t31.h>

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t31_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t31_spl_putc(hex[(v >> i) & 0xf]);
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

gd_t gdata __section(".bss");

void board_init_f(ulong dummy)
{
	char *p;

	/*
	 * Zero BSS - start.S jumps straight here without clearing it.
	 * DM-in-SPL needs a clean gd / BSS before spl_init() brings
	 * driver model up.
	 */
	for (p = __bss_start; p < __bss_end; p++)
		*p = 0;
	gd = &gdata;

	/*
	 * The mask ROM leaves a usable EXTAL-based clock, so the console
	 * works before pll_init() - bring it up first so any later hang
	 * still produces output.
	 */
	clk_ungate_uart(T31_CONSOLE_UART);
	t31_spl_serial_init();

	/*
	 * Make the FDT blob available (OF_SEPARATE: appended after the SPL) so
	 * the DM scan can bind devices and the UCLASS_RAM probe can read the
	 * per-SKU "ingenic,sdram-params" array from the &ddr node.
	 */
	if (fdtdec_setup())
		hang();

	/*
	 * Bring driver model up and probe the UCLASS_RAM driver. T31 is the
	 * uncapped SoC, so this single SPL is the first loader stage: its probe
	 * sets the PLLs and brings DDR up from the DT params
	 * (drivers/ram/ingenic/ddr_t31.c), then records the size. spl_init here
	 * needs the enlarged SPL-f heap (SYS_MALLOC_F_LEN) for the DM scan, since
	 * the DRAM malloc is not up until board_init_r.
	 */
	if (spl_init())
		hang();
	{
		struct udevice *dev;
		struct ram_info ram;

		if (uclass_first_device_err(UCLASS_RAM, &dev))
			hang();
		if (ram_get_info(dev, &ram))
			hang();
		dram_verify((u32)ram.size);
	}

#ifdef CONFIG_SPL_T31_USB_BOOT
	/*
	 * USB-boot stage1: clocks and DDR are up. Set up the SFC clock so
	 * U-Boot proper (uploaded to DRAM by the mask ROM) can probe NOR,
	 * then return into the mask ROM USB loop (start.S kept the bootrom
	 * sp, so a plain jr ra resumes it).
	 */
	t31_spl_sfc_clk_init();
	return;
#else
	/*
	 * NOR cold-boot: bring driver model up, then hand off to the
	 * standard SPL framework board_init_r(), which loads
	 * u-boot-lzma.img from CONFIG_SYS_SPI_U_BOOT_OFFS via the SPI
	 * flash uclass (spl_boot_device() == BOOT_DEVICE_SPI),
	 * LZMA-decompresses it and jumps. Does not return.
	 */
	/*
	 * DDR is up, so hand off to the standard SPL framework
	 * board_init_r(). It sets up the DRAM malloc heap, brings driver
	 * model up (spl_init) and loads u-boot-lzma.img from
	 * CONFIG_SYS_SPI_U_BOOT_OFFS via the DM SFC driver
	 * (spl_boot_device() == BOOT_DEVICE_SPI), LZMA-decompresses it and
	 * jumps. Driver model is deferred to board_init_r (not called here)
	 * so the DM scan / autoprobe runs against the full DRAM malloc, not
	 * the tiny SPL-f heap - T31 needs no DM in board_init_f because DDR
	 * is hand-rolled.
	 */
	preloader_console_init();
	t31_spl_sfc_clk_init();
	board_init_r(NULL, 0);
	__builtin_unreachable();
#endif
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_SPI;
}
#endif /* CONFIG_XPL_BUILD */
