// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 SoC SPL bring-up (XBurst2)
 *
 * The SPL runs from ~34 KB of on-chip SRAM with no driver model. It
 * brings up a minimal console, configures the PLLs, inits DDR and then
 * loads U-Boot proper into DRAM. Full U-Boot uses driver model.
 */

#include <config.h>
#include <stdio.h>
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
void a1_spl_load_uboot(void);
void a1_spl_sfc_clk_init(void);

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
		0xdeadbeef, 0x12345678, 0xa5a5a5a5, 0x5a5a5a5a,
		0xffffffff, 0x00000000,
	};
	volatile u32 *a;
	u32 off;
	int p;

	/* Pattern test at the base word (uncached - direct to DRAM). */
	a = (volatile u32 *)0xa0000000;
	for (p = 0; p < (int)ARRAY_SIZE(pat); p++) {
		*a = pat[p];
		u32 rd = *a;
		if (rd != pat[p]) {
			a1_spl_puts("FAIL pat @");
			spl_put_hex((u32)(uintptr_t)a);
			a1_spl_puts(" w=");
			spl_put_hex(pat[p]);
			a1_spl_puts(" r=");
			spl_put_hex(rd);
			a1_spl_putc('\n');
			return -1;
		}
	}

	/*
	 * Address-in-data sweep across all 256 MB, one word per 1 MB.
	 * The whole range is written before any of it is read back, so
	 * this also catches address aliasing - a marginally trained
	 * DRAM can pass a single-location test yet still alias.
	 */
	for (off = 0; off < A1_DRAM_SIZE; off += 0x100000) {
		a = (volatile u32 *)(uintptr_t)(0xa0000000u + off);
		*a = 0xa0000000u + off;
	}
	for (off = 0; off < A1_DRAM_SIZE; off += 0x100000) {
		a = (volatile u32 *)(uintptr_t)(0xa0000000u + off);
		if (*a != 0xa0000000u + off) {
			a1_spl_puts("FAIL sweep @");
			spl_put_hex((u32)(uintptr_t)a);
			a1_spl_puts(" r=");
			spl_put_hex(*a);
			a1_spl_putc('\n');
			return -1;
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

#ifdef CONFIG_SPL_A1_USB_BOOT
	/*
	 * USB-boot stage1: clocks and DDR are up. Also bring up the SFC0
	 * clock so U-Boot proper (uploaded into DRAM next) can probe the
	 * SPI-NOR - the NOR-boot SPL gets this via sfc_nor_load(). Then
	 * return into the mask ROM: start.S keeps the bootrom sp for this
	 * build, so the SPL ran as a normal nested call and a plain
	 * return (jr ra) resumes the bootrom USB loop, which uploads
	 * U-Boot proper.
	 */
	a1_spl_sfc_clk_init();
	a1_spl_puts("A1 SPL: returning to mask ROM (USB boot)\n");
	return;
#else
	a1_spl_puts("A1 SPL: loading U-Boot...\n");
	a1_spl_load_uboot();
	for (;;)
		;
#endif
}
#endif /* CONFIG_XPL_BUILD */

#ifndef CONFIG_XPL_BUILD
/*
 * SoC reset - overrides the weak _machine_restart() in
 * arch/mips/cpu/cpu.c (reached via do_reset -> reset_cpu).
 *
 * Arms the TCU watchdog one-shot; the timeout expiry resets the whole
 * SoC. Quirks, all confirmed on real A1 silicon:
 *  - the WDT counter accepts only the 32 kHz RTC clock (the PCLK and
 *    EXTAL TCSR source-select bits are rejected by the hardware);
 *  - the timeout (TDR) must not be tiny, or the counter overshoots the
 *    compare value before the comparator engages and then has to wrap
 *    the full 16 bits (~128 s) before it fires;
 *  - TDR/TCSR must settle into the slow RTC clock domain before the
 *    counter is enabled. udelay() hangs on the reset path here, so the
 *    settle is a plain spin loop with no timer dependency.
 */
void _machine_restart(void)
{
	void __iomem *tcu = (void __iomem *)TCU_BASE;
	volatile int i;

	puts("resetting...\n");

	writel(1 << 16, tcu + 0x3c);		/* TSCR: start the WDT clock */
	writel(0, tcu + 0x08);			/* TCNT = 0 */
	writel(0x100, tcu + 0x00);		/* TDR: ~0.5 s at RTC 32k/64 */
	writel((3 << 3) | (1 << 1), tcu + 0x0c);/* TCSR: /64 prescale, RTC */

	for (i = 0; i < 100000; i++)		/* settle into the RTC domain */
		;

	writel(0, tcu + 0x04);			/* TCER: counter off */
	writel(1 << 0, tcu + 0x04);		/* TCER: counter on -> fires */

	for (;;)
		;
}
#endif /* !CONFIG_XPL_BUILD */
