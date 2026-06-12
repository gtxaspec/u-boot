// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 SoC SPL bring-up
 *
 * The SPL runs from on-chip SRAM (>= 128 KB: the vendor BIG_SPL build
 * runs 100 KB SPLs pre-DDR and places BSS at 0x8001f000). It brings up
 * a minimal console, configures the PLLs, then brings driver model up:
 * the UCLASS_RAM driver (drivers/ram/ingenic/ddr_t32.c) probes off the
 * SPL devicetree and inits DDR, and U-Boot loading goes through the
 * standard SPL_SPI framework: board_init_r() reads U-Boot proper from
 * SPI-NOR via the DM SFC driver, LZMA-decompresses it and jumps. Full
 * U-Boot uses driver model. Forward-ported from the vendor U-Boot
 * 2022.10 T32 spl.c.
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

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void t32_spl_serial_init(void);
void t32_spl_puts(const char *s);
void t32_spl_putc(char c);
void t32_spl_sfc_clk_init(void);

#ifdef CONFIG_XPL_BUILD
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
			volatile u32 *a =
				(volatile u32 *)(bases[b] + offs[o]);

			for (p = 0; p < (int)ARRAY_SIZE(pat); p++) {
				*a = pat[p];
				if (*a != pat[p]) {
					t32_spl_puts("T32 SPL: DDR FAIL @");
					spl_put_hex((u32)(uintptr_t)a);
					t32_spl_puts(" wrote ");
					spl_put_hex(pat[p]);
					t32_spl_puts(" read ");
					spl_put_hex(*a);
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
	/*
	 * Zero BSS - start.S jumps straight here without clearing it.
	 * DM-in-SPL needs a clean gd / BSS before spl_init() brings
	 * driver model up.
	 */
	memset(__bss_start, 0, (size_t)__bss_end - (size_t)__bss_start);
	gd = &gdata;

	/*
	 * The mask ROM leaves a usable EXTAL-based clock, so the console
	 * works before pll_init() - bring it up first so any later hang
	 * still produces output.
	 */
	clk_ungate_uart(T32_CONSOLE_UART);
	t32_spl_serial_init();

	/*
	 * Vendor T32 spl.c pre-PLL pokes: clear the OST gate bit (the
	 * vendor clears CPM_CLKGR1_OST within CLKGR0), disable the
	 * watchdog, and set the low MESTSEL bits.
	 */
	writel(readl((void __iomem *)(CPM_BASE + CPM_CLKGR0)) &
		   ~CPM_CLKGR1_OST,
	       (void __iomem *)(CPM_BASE + CPM_CLKGR0));
	writel(0, (void __iomem *)(WDT_BASE + WDT_TCER));
	writel(readl((void __iomem *)(CPM_BASE + CPM_MESTSEL)) | 0x7,
	       (void __iomem *)(CPM_BASE + CPM_MESTSEL));

	/*
	 * Make the FDT blob available (OF_SEPARATE: appended after the SPL)
	 * so pll_init() can match the DDR node's per-SKU compatible and pick
	 * the per-SKU PLL setpoints before driver model comes up.
	 */
	if (fdtdec_setup())
		hang();

	pll_init();

	/*
	 * Bring driver model up and probe the UCLASS_RAM driver, whose SPL
	 * probe runs the uMCTL2/Innophy sdram_init to bring up DDR -
	 * replaces the old direct sdram_init() call, mirroring the
	 * T31/XBurst2 flow. spl_init here needs the enlarged SPL-f heap
	 * (SYS_MALLOC_F_LEN) for the DM scan, since the DRAM malloc is not
	 * up until board_init_r.
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

#ifdef CONFIG_SPL_T32_USB_BOOT
	/*
	 * USB-boot stage1: clocks and DDR are up. Set up the SFC clock so
	 * U-Boot proper (uploaded to DRAM by the mask ROM) can probe NOR,
	 * then return into the mask ROM USB loop (start.S kept the bootrom
	 * sp, so a plain jr ra resumes it).
	 */
	t32_spl_sfc_clk_init();
	return;
#else
	/*
	 * NOR cold-boot: DDR is up, so hand off to the standard SPL
	 * framework board_init_r(). It sets up the DRAM malloc heap and
	 * loads u-boot-lzma.img from CONFIG_SYS_SPI_U_BOOT_OFFS via the
	 * DM SFC driver (spl_boot_device() == BOOT_DEVICE_SPI),
	 * LZMA-decompresses it and jumps. Does not return.
	 */
	preloader_console_init();
	t32_spl_sfc_clk_init();
	board_init_r(NULL, 0);
	__builtin_unreachable();
#endif
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_SPI;
}
#endif /* CONFIG_XPL_BUILD */
