// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 SoC SPL bring-up (XBurst2)
 *
 * The SPL runs from ~34 KB of on-chip SRAM with no driver model. It
 * brings up a minimal console, configures the PLLs, inits DDR and then
 * loads U-Boot proper into DRAM. Full U-Boot uses driver model.
 */

#include <config.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/a1.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void a1_spl_serial_init(void);
void a1_spl_puts(const char *s);
void a1_spl_putc(char c);
void __weak sdram_init(void) { }

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	a1_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		a1_spl_putc(hex[(v >> i) & 0xf]);
}

#define A1_DRAM_SIZE	0x10000000u	/* 256 MB */

static int dram_verify(void)
{
	static const u32 pat[] = {
		0x00000000, 0xffffffff, 0xa5a5a5a5, 0x5a5a5a5a,
		0xdeadbeef, 0x12345678,
	};
	const u32 bases[] = { 0xa0000000, 0x80000000 };
	const u32 offs[] = { 0x0, 0x4, 0x100000,
			     A1_DRAM_SIZE / 2, A1_DRAM_SIZE - 4 };
	int b, o, p;

	for (b = 0; b < 2; b++) {
		for (o = 0; o < (int)ARRAY_SIZE(offs); o++) {
			volatile u32 *a =
				(volatile u32 *)(bases[b] + offs[o]);

			for (p = 0; p < (int)ARRAY_SIZE(pat); p++) {
				*a = pat[p];
				if (*a != pat[p]) {
					a1_spl_puts("A1 SPL: DDR FAIL @");
					spl_put_hex((u32)(uintptr_t)a);
					a1_spl_puts(" wrote ");
					spl_put_hex(pat[p]);
					a1_spl_puts(" read ");
					spl_put_hex(*a);
					a1_spl_putc('\n');
					return -1;
				}
			}
		}
	}
	return 0;
}
#endif

#ifdef CONFIG_XPL_BUILD
gd_t gdata __section(".bss");

void board_init_f(ulong dummy)
{
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
	a1_spl_puts("\nA1 SPL: alive (pre-PLL)\n");

	pll_init();
	a1_spl_puts("A1 SPL: PLL configured\n");

	sdram_init();
	if (dram_verify() == 0)
		a1_spl_puts("A1 SPL: DDR OK\n");

	a1_spl_puts("A1 SPL: halting (no SFC loader yet)\n");
	for (;;)
		;
}
#endif /* CONFIG_XPL_BUILD */
