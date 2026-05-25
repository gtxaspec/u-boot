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
void t40_spl_puts(const char *s);
void t40_spl_putc(char c);
void __weak sdram_init(void) { }
void t40_spl_load_uboot(void);
void t40_spl_sfc_clk_init(void);
int timer_init(void);

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t40_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t40_spl_putc(hex[(v >> i) & 0xf]);
}

#define T40_DRAM_SIZE	(CONFIG_T40_DRAM_SIZE_MB * 1024u * 1024u)

static int dram_verify(void)
{
	static const u32 pat[] = {
		0xdeadbeef, 0x12345678, 0xa5a5a5a5, 0x5a5a5a5a,
		0xffffffff, 0x00000000,
	};
	volatile u32 *a;
	u32 off;
	int p;

	a = (volatile u32 *)0xa0000000;
	for (p = 0; p < (int)ARRAY_SIZE(pat); p++) {
		*a = pat[p];
		if (*a != pat[p])
			return -1;
	}

	/* Coarse sweep across the part, one word per 1 MB. */
	for (off = 0; off < T40_DRAM_SIZE; off += 0x100000) {
		a = (volatile u32 *)(uintptr_t)(0xa0000000u + off);
		*a = 0xa0000000u + off;
	}
	for (off = 0; off < T40_DRAM_SIZE; off += 0x100000) {
		a = (volatile u32 *)(uintptr_t)(0xa0000000u + off);
		if (*a != 0xa0000000u + off)
			return -1;
	}

	return 0;
}

gd_t gdata __section(".bss");

void board_init_f(ulong dummy)
{
	gd = &gdata;

	/* Disable watchdog */
	writel(0, (void __iomem *)(WDT_BASE + 0x04));

	/*
	 * XBurst2 CCU tweaks - mirrors A1's HW-validated sequence (T40 is
	 * the XBurst2 sibling of A1, NOT XBurst1, so the same prefetcher-
	 * trust / IFU-loop quirks apply on real silicon):
	 *   CCU +0xfe0: |= 0x78  disable IFU simple loop (bits 6:3)
	 *   CCU +0x060: |= 0x10  disable L1 prefetcher trust (bit 4)
	 *
	 * The vendor T40 U-Boot 2013 source only writes 0x18 to +0xfe0 and
	 * does not touch +0x060 - that source was QEMU-shaped and not
	 * proven against real T40NN silicon; mirroring A1's HW-validated
	 * sequence is what makes the SPL alive on real T40NN silicon.
	 */
	{
		u32 v = readl((void __iomem *)(CCU_BASE + 0xfe0));
		writel(v | 0x78, (void __iomem *)(CCU_BASE + 0xfe0));

		v = readl((void __iomem *)(CCU_BASE + 0x060));
		writel(v | 0x10, (void __iomem *)(CCU_BASE + 0x060));
	}

	clk_ungate_uart(T40_CONSOLE_UART);
	t40_spl_serial_init();
	t40_spl_puts("\nT40 SPL: alive (pre-PLL)\n");

	pll_init();
	t40_spl_puts("T40 SPL: PLL configured\n");

	timer_init();
	sdram_init();
	if (dram_verify() == 0)
		t40_spl_puts("T40 SPL: DDR OK\n");
	else
		t40_spl_puts("T40 SPL: DDR verify FAILED\n");

#ifdef CONFIG_SPL_T40_USB_BOOT
	t40_spl_sfc_clk_init();
	t40_spl_puts("T40 SPL: returning to mask ROM (USB boot)\n");
	return;
#else
	t40_spl_puts("T40 SPL: loading U-Boot...\n");
	t40_spl_load_uboot();
	for (;;)
		;
#endif
}
#endif /* CONFIG_XPL_BUILD */
