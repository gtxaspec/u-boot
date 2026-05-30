// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T40 SoC SPL bring-up (XBurst2)
 *
 * The SPL runs from on-chip SRAM with no driver model. It brings up
 * a minimal console, configures the PLLs, inits DDR and then loads
 * U-Boot proper into DRAM. Full U-Boot uses driver model.
 *
 * T40 is the XBurst2 sibling of A1: same SPL skeleton, different
 * register reshuffle (single OTG, Synopsys GMAC, 4 UARTs, T40-
 * specific CPM offsets). Forward-ported from the vendor U-Boot
 * 2013 T40-1.3.1 branch.
 */

#include <config.h>
#include <dm.h>
#include <fdtdec.h>
#include <hang.h>
#include <init.h>
#include <spl.h>
#include <stdio.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t40.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void t40_spl_serial_init(void);
void t40_spl_sfc_clk_init(void);
int timer_init(void);

#ifdef CONFIG_XPL_BUILD
gd_t gdata __section(".bss");

extern char __bss_start[], __bss_end[];

void board_init_f(ulong dummy)
{
	char *p;

	/*
	 * Zero BSS - our SPL crt0 (arch/mips/mach-xburst/start.S T40
	 * branch) jumps straight here without clearing the BSS region.
	 * Letting BSS contain bootrom-left SRAM garbage broke T40XP
	 * USB-boot: gd->cyclic.cyclic_list ended up with non-empty
	 * pointers, so mdelay/udelay -> schedule() -> cyclic_run()
	 * walked into bad memory and never returned. T40N happened to
	 * have a zero list head from a slightly different bootrom code
	 * path leaving SRAM mostly clean. Zero BSS up front so SPL
	 * state starts deterministic on every chip variant.
	 */
	for (p = __bss_start; p < __bss_end; p++)
		*p = 0;

	gd = &gdata;

	/* Disable watchdog */
	writel(0, (void __iomem *)(WDT_BASE + 0x04));

	/*
	 * XBurst2 CCU tweak - exactly match vendor T40 U-Boot 2013
	 * (board_init_f, 0x80001ccc): CCU +0xfe0 |= 0x18 (bits 3,4 -
	 * IFU simple-loop / prefetcher tweak). Vendor does NOT touch
	 * CCU +0x060.
	 *
	 * 2026-05-26: prior code wrote |= 0x78 to +0xfe0 and |= 0x10
	 * to +0x060 (borrowed from A1 USB-boot bring-up). Those worked
	 * for USB-boot SPL but cold SFC NOR boot was silent. Vendor T40
	 * SFCNOR binary cold-boots fine with just |= 0x18 to +0xfe0,
	 * so revert to vendor's actual values.
	 */
	{
		u32 v = readl((void __iomem *)(CCU_BASE + 0xfe0));
		writel(v | 0x18, (void __iomem *)(CCU_BASE + 0xfe0));
	}

	clk_ungate_uart(T40_CONSOLE_UART);
	t40_spl_serial_init();

	/*
	 * Make the FDT blob available (OF_SEPARATE: appended after the SPL)
	 * so pll_init() can read ingenic,variant from the DDR node and pick
	 * the per-SKU PLL setpoints before driver model comes up.
	 */
	if (fdtdec_setup())
		hang();

	pll_init();

	timer_init();

	/* Bring driver model up so the UCLASS_RAM driver in
	 * drivers/ram/ingenic/ can probe off the memory-controller node
	 * in DT. spl_init() runs dm_init_and_scan() + dm_autoprobe();
	 * we then explicitly probe the RAM uclass to bring DRAM up. */
	if (spl_init())
		hang();

	{
		struct udevice *dev;

		if (uclass_first_device_err(UCLASS_RAM, &dev))
			hang();
	}

	preloader_console_init();

	/* Re-program CPM_SFCCDR so the SFC SPI driver (used for both the
	 * cold-boot SFC NOR load and any DM operations against the flash
	 * later) has a usable clock. The bootrom configured the SFC for
	 * its own load, but our pll_init / CGU re-source above may have
	 * left SFCCDR in a stale state. */
	t40_spl_sfc_clk_init();

#ifdef CONFIG_SPL_T40_USB_BOOT
#ifdef CONFIG_SPL_T40_NAND_PROBE
	{
		extern void sfc_nand_probe_dump(void);
		printf("T40 SPL: probing SPI-NAND...\n");
		sfc_nand_probe_dump();
	}
#endif
	/* USB-boot dev path: return to mask ROM. The bootrom uploads
	 * U-Boot proper to 0x80100000 and jumps to it. */
	return;
#elif defined(CONFIG_SPL_T40_SFC_NAND_BOOT)
	/* SFC NAND cold-boot (T40XP): DDR is up via UCLASS_RAM, the SPL
	 * framework's malloc heap is in DRAM. Mainline U-Boot has no
	 * generic DM SPI-NAND SPL loader, so call the custom NAND loader
	 * in sfc_nand.c which reads the legacy mkimage header from the
	 * boot NAND, LZMA-decompresses to CONFIG_TEXT_BASE, and jumps. */
	{
		extern void t40_spl_nand_load_uboot(void);

		t40_spl_nand_load_uboot();
		hang();
	}
#else
	/* SFC NOR cold-boot: hand off to the standard SPL framework
	 * board_init_r(). It runs boot_from_devices() against
	 * spl_boot_device() (BOOT_DEVICE_SPI below), which uses the SPI
	 * flash uclass to load u-boot-lzma.img from NOR offset
	 * CONFIG_SYS_SPI_U_BOOT_OFFS, LZMA-decompresses to DRAM and
	 * jumps. Does not return. */
	board_init_r(NULL, 0);
	__builtin_unreachable();
#endif
}

#ifndef CONFIG_SPL_T40_USB_BOOT
u32 spl_boot_device(void)
{
	return BOOT_DEVICE_SPI;
}
#endif
#endif /* CONFIG_XPL_BUILD */
