// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T41 SoC SPL bring-up (XBurst2)
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
#include <hang.h>
#include <init.h>
#include <spl.h>
#include <stdio.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t41.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void t41_spl_serial_init(void);
void t41_spl_sfc_clk_init(void);
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

	clk_ungate_uart(T41_CONSOLE_UART);
	t41_spl_serial_init();

	/* Full vendor clk_prepare: stop ALL CGU clocks before pll_init */
	{
		/* CGU offsets: {reg, ce_bit, busy_bit, stop_bit} - all same: 29,28,27 */
		static const u32 cgus[] = {
			0x2c, /* DDR */ 0xbc, /* EL200 */ 0x54, /* MACPHY */
			0x64, /* LCD */ 0xfc, /* PWM */ 0x60, /* SFC0 */
			0x7c, /* SFC1 */ 0x90, /* CIM0 */ 0xac, /* ISPA */
			0xa8, /* ISPS */ 0x80, /* ISPM */ 0x70, /* I2S */
		};
		int c; u32 r;
		for (c = 0; c < (int)(sizeof(cgus)/sizeof(cgus[0])); c++) {
			r = readl((void __iomem *)(CPM_BASE + cgus[c]));
			r |= 0xff | (1 << 29);
			writel(r, (void __iomem *)(CPM_BASE + cgus[c]));
			while (readl((void __iomem *)(CPM_BASE + cgus[c])) & (1 << 28));
			r = readl((void __iomem *)(CPM_BASE + cgus[c]));
			r |= (1 << 27) | (1 << 29);
			writel(r, (void __iomem *)(CPM_BASE + cgus[c]));
			while (readl((void __iomem *)(CPM_BASE + cgus[c])) & (1 << 28));
			r = readl((void __iomem *)(CPM_BASE + cgus[c]));
			r &= ~((1 << 29) | (1 << 27));
			writel(r, (void __iomem *)(CPM_BASE + cgus[c]));
		}
	}

	pll_init();

	/* Full vendor clk_init: set PLL source for ALL CGUs.
	 * Format: {reg_offset, sel_index << 30} */
	{
		static const struct { u32 off; u32 sel; } src[] = {
			{0x2c, 2u<<30}, /* DDR -> MPLL (idx 2) */
			{0xbc, 0u<<30}, /* EL200 -> APLL (idx 0) */
			{0x54, 1u<<30}, /* MACPHY -> MPLL (idx 1) */
			{0x64, 2u<<30}, /* LCD -> VPLL (idx 2) */
			{0xfc, 2u<<30}, /* PWM -> VPLL (idx 2) */
			{0x60, 1u<<30}, /* SFC0 -> MPLL (idx 1) */
			{0x7c, 1u<<30}, /* SFC1 -> MPLL (idx 1) */
			{0x90, 0u<<30}, /* CIM0 -> APLL (idx 0) */
			{0xac, 1u<<30}, /* ISPA -> MPLL (idx 1) */
			{0xa8, 1u<<30}, /* ISPS -> MPLL (idx 1) */
			{0x80, 1u<<30}, /* ISPM -> MPLL (idx 1) */
			{0x70, 2u<<30}, /* I2S -> VPLL (idx 2) */
		};
		int c; u32 r;
		for (c = 0; c < (int)(sizeof(src)/sizeof(src[0])); c++) {
			r = readl((void __iomem *)(CPM_BASE + src[c].off));
			r &= ~(3u << 30);
			r |= src[c].sel;
			writel(r, (void __iomem *)(CPM_BASE + src[c].off));
		}
	}

	timer_init();

	/* Bring driver model up so the UCLASS_RAM driver in
	 * drivers/ram/ingenic/ can probe off the memory-controller node
	 * in DT. spl_init() runs dm_init_and_scan() and dm_autoprobe()
	 * which together find the device, look up the variant struct via
	 * the compatible string, and call the driver's .probe (which in
	 * SPL phase runs ingenic_ddr_sdram_init() against the variant). */
	if (spl_init())
		hang();

	/* The CGU DDR clock divider (CPM_DDRCDR) has to be programmed
	 * before the RAM driver's PHY PLL setup since the Innophy PHY
	 * takes the CGU output as its input clock. The driver probe is
	 * triggered explicitly here so we have a chance to do that just
	 * beforehand. */
	{
		struct udevice *dev;

		if (uclass_first_device_err(UCLASS_RAM, &dev))
			hang();
	}

	/* Switch to DM serial output so board_init_r framework printf
	 * goes somewhere visible. spl_init already brought DM up; this
	 * probes the UART uclass and points printf at it. */
	preloader_console_init();

	/* Re-program CPM_SFCCDR so the SFC SPI driver (used for both the
	 * cold-boot SFC NOR load and any DM operations against the flash
	 * later) has a usable clock. The bootrom used the SFC to load the
	 * SPL, but our pll_init / CGU re-source above may have left
	 * SFCCDR in a stale state. vendor t41_spl_sfc_clk_init() programs
	 * source/div/CE consistent with the SFC controller's expectation. */
	t41_spl_sfc_clk_init();

#ifdef CONFIG_SPL_T41_USB_BOOT
	/* USB-boot dev path: return to mask ROM. The bootrom uploads
	 * U-Boot proper to 0x80100000 and jumps to it. We intentionally
	 * skip board_init_r() - SPL framework's board_init_r() never
	 * returns, but the vendor USB-boot pattern requires the mask ROM
	 * to take control back after DRAM is up so it can upload U-Boot. */
	return;
#else
	/* SFC NOR cold-boot: hand off to the standard SPL framework
	 * board_init_r(). It runs boot_from_devices() against
	 * spl_boot_device() (BOOT_DEVICE_SPI below), which uses the SPI
	 * flash uclass to load u-boot-lzma.img from NOR offset
	 * CONFIG_SYS_SPI_U_BOOT_OFFS to DRAM, decompresses LZMA, and
	 * jumps. Does not return. */
	board_init_r(NULL, 0);
	__builtin_unreachable();
#endif
}

#ifndef CONFIG_SPL_T41_USB_BOOT
u32 spl_boot_device(void)
{
	return BOOT_DEVICE_SPI;
}
#endif

#endif /* CONFIG_XPL_BUILD */
