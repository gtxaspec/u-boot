// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 SoC SPL bring-up
 *
 * The SPL runs from on-chip SRAM with no driver model: minimal
 * console, PLLs, DDR, then load U-Boot proper. Stage 1 brings up
 * console + PLL and (CONFIG_SPL_T32_USB_BOOT) returns to the mask
 * ROM; the Innophy DDR2 (Stage 2) + SFC NOR-boot (Stage 3) land
 * next. Forward-ported from the vendor U-Boot 2022.10 T32 spl.c;
 * full U-Boot uses driver model.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t32.h>
#include <mach/t32-ddr.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void t32_spl_serial_init(void);
void t32_spl_puts(const char *s);
void t32_spl_putc(char c);
void t32_spl_load_uboot(void);
void __weak sdram_init(void) { }

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
 * cached (KSEG0) window and verify the read-back, stepping across the
 * whole part (64 MB DDR2 / 128 MB DDR3 / 256 MB DDR3-W632 / 128 MB
 * LPDDR3) so a stuck/aliased address line is caught.
 */
#define T32_DRAM_SIZE	T32_DDR_SIZE	/* per Kconfig DDR class */

static int dram_verify(void)
{
	static const u32 pat[] = {
		0x00000000, 0xffffffff, 0xa5a5a5a5, 0x5a5a5a5a,
		0xdeadbeef, 0x12345678,
	};
	const u32 bases[] = { 0xa0000000, 0x80000000 };
	const u32 offs[] = { 0x0, 0x4, 0x100000,
			     T32_DRAM_SIZE / 2, T32_DRAM_SIZE - 4 };
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
	u32 socid;

	gd = &gdata;
	memset(__bss_start, 0, (size_t)__bss_end - (size_t)__bss_start);

	/*
	 * The mask ROM leaves a usable EXTAL-based clock, so the console
	 * works before pll_init() - bring it up first so any later hang
	 * still produces output.
	 */
	clk_ungate_uart(T32_CONSOLE_UART);
	t32_spl_serial_init();
	t32_spl_puts("\nT32 SPL: alive (pre-PLL)\n");

	socid = readl((void __iomem *)T32_SOCID_ADDR);
	t32_spl_puts("T32 SPL: SOCID ");
	spl_put_hex(socid);
	t32_spl_puts(socid == T32_SOCID ? " (T32)\n" : " (unexpected)\n");

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

	pll_init();
	t32_spl_puts("T32 SPL: PLL configured\n");

	/*
	 * Ungate the DDR controller clock (CPM_CLKGR0 bit 27).
	 * Without this the uMCTL2 controller has no APB/AXI clock,
	 * stays in init state forever, and the PHY-training STAT
	 * poll spins. Vendor T32 clk.c clk_init() clears the same
	 * bit before any DDR access.
	 */
	writel(readl((void __iomem *)(CPM_BASE + CPM_CLKGR0)) & ~BIT(27),
	       (void __iomem *)(CPM_BASE + CPM_CLKGR0));

	sdram_init();
	if (dram_verify() == 0)
		t32_spl_puts("T32 SPL: DDR OK\n");
	else
		t32_spl_puts("T32 SPL: DDR verify FAILED\n");

#ifdef CONFIG_SPL_T32_USB_BOOT
	/*
	 * USB-boot stage1: clocks are up. Return to the mask ROM
	 * (start.S branches in, so $ra still holds the bootrom return
	 * address and this epilogue jr's back into the bootrom USB
	 * loop). The host then uploads U-Boot proper over USB.
	 */
	t32_spl_puts("T32 SPL: returning to mask ROM (USB boot)\n");
	return;
#else
	/* Load U-Boot proper from SPI-NOR into DRAM and jump. */
	t32_spl_puts("T32 SPL: loading U-Boot...\n");
	t32_spl_load_uboot();
	for (;;)
		;
#endif
}
#endif /* CONFIG_XPL_BUILD */
