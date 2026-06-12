// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 SoC SPL bring-up (XBurst2)
 *
 * Running from on-chip SRAM, the SPL sets up the early UART, configures
 * the PLLs, then brings up driver model (DM-in-SPL) to init DDR via the
 * UCLASS_RAM driver and load U-Boot proper from SPI-NOR.
 */

#include <config.h>
#include <dm.h>
#include <fdtdec.h>
#include <hang.h>
#include <init.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/a1.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
int timer_init(void);
void clk_ungate_uart(unsigned int idx);
void a1_spl_serial_init(void);
void a1_spl_sfc_clk_init(void);

#ifdef CONFIG_XPL_BUILD
gd_t gdata __section(".bss");

void board_init_f(ulong dummy)
{
	char *p;

	/*
	 * Zero BSS - start.S jumps straight here without clearing it.
	 * DM-in-SPL needs a clean gd / BSS (stale SRAM left in gd->cyclic
	 * broke T40XP USB-boot the same way), so clear it before spl_init()
	 * brings driver model up.
	 */
	for (p = __bss_start; p < __bss_end; p++)
		*p = 0;

	gd = &gdata;

	/* Disable watchdog */
	writel(0, (void __iomem *)(WDT_BASE + 0x04));

	/*
	 * XBurst2 CCU tweaks (vendor a1.c board_init_f):
	 * - CCU +0xfe0: set bits 6:3 to disable IFU simple loop
	 * - CCU +0x060: set bit 4 to disable L1 prefetcher trust
	 */
	u32 ccu_val = readl((void __iomem *)(CCU_BASE + 0xfe0));
	writel(ccu_val | 0x78, (void __iomem *)(CCU_BASE + 0xfe0));

	ccu_val = readl((void __iomem *)(CCU_BASE + 0x060));
	writel(ccu_val | 0x10, (void __iomem *)(CCU_BASE + 0x060));

	clk_ungate_uart(A1_CONSOLE_UART);
	a1_spl_serial_init();

	/*
	 * Make the FDT blob available (OF_SEPARATE: appended after the SPL)
	 * so pll_init() can match the DDR node's per-SKU compatible and pick
	 * the per-SKU PLL setpoints before driver model comes up.
	 */
	if (fdtdec_setup())
		hang();

	pll_init();

	/*
	 * Start the Global OST timebase before DM/DDR: the UCLASS_RAM DDR
	 * driver uses the generic udelay()/mdelay(), which spin forever on
	 * XBurst2 (no CP0 Count) until the OST is running.
	 */
	timer_init();

	/*
	 * Bring driver model up so the UCLASS_RAM driver in
	 * drivers/ram/ingenic/ probes off the memory-controller node in the
	 * SPL device tree and runs ingenic_ddr_sdram_init() (A1 family)
	 * against the variant selected by the DDR node's per-SKU compatible.
	 * Replaces the old direct sdram_init() call.
	 */
	if (spl_init())
		hang();
	{
		struct udevice *dev;

		if (uclass_first_device_err(UCLASS_RAM, &dev))
			hang();
	}

	/* Switch printf to the DM console for the rest of SPL. */
	preloader_console_init();

	/*
	 * Bring up the SFC0 clock for the SPI driver: needed by the NOR
	 * cold-boot SPL_SPI load below, and (USB-boot) by U-Boot proper's
	 * NOR probe after the mask ROM jumps to it.
	 */
	a1_spl_sfc_clk_init();

#ifdef CONFIG_SPL_A1_USB_BOOT
	/*
	 * USB-boot stage1: clocks and DDR are up. Return into the mask ROM:
	 * start.S keeps the bootrom sp for this build, so the SPL ran as a
	 * normal nested call and a plain return (jr ra) resumes the bootrom
	 * USB loop, which uploads U-Boot proper to DRAM and jumps.
	 */
	return;
#else
	/*
	 * SFC NOR cold-boot: hand off to the standard SPL framework
	 * board_init_r(), which loads u-boot-lzma.img from
	 * CONFIG_SYS_SPI_U_BOOT_OFFS via the SPI flash uclass
	 * (spl_boot_device() == BOOT_DEVICE_SPI), LZMA-decompresses it and
	 * jumps. Does not return.
	 */
	board_init_r(NULL, 0);
	__builtin_unreachable();
#endif
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_SPI;
}
#endif /* CONFIG_XPL_BUILD */
