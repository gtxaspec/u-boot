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
void t41_spl_puts(const char *s);
void t41_spl_putc(char c);
void __weak sdram_init(void) { }
void t41_spl_load_uboot(void);
void t41_spl_sfc_clk_init(void);
int timer_init(void);

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t41_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t41_spl_putc(hex[(v >> i) & 0xf]);
}

#define T41_DRAM_SIZE	(CONFIG_T41_DRAM_SIZE_MB * 1024u * 1024u)

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
	*a = 0xdeadbeef;
	{
		u32 got = *a;
		static const char hc[] = "0123456789abcdef";
		char buf[14];
		int j;
		buf[0] = '0'; buf[1] = 'x';
		for (j = 7; j >= 0; j--)
			buf[9 - j] = hc[(got >> (j * 4)) & 0xf];
		buf[10] = '\n'; buf[11] = 0;
		t41_spl_puts("RD=");
		t41_spl_puts(buf);
		if (got != 0xdeadbeef) {
			t41_spl_puts("DDR: pat fail\n");
			return -1;
		}
	}
	for (p = 1; p < (int)ARRAY_SIZE(pat); p++) {
		*a = pat[p];
		if (*a != pat[p]) {
			t41_spl_puts("DDR: pat fail\n");
			return -1;
		}
	}

	/* Coarse sweep across the part, one word per 1 MB. */
	for (off = 0; off < T41_DRAM_SIZE; off += 0x100000) {
		a = (volatile u32 *)(uintptr_t)(0xa0000000u + off);
		*a = 0xa0000000u + off;
	}
	for (off = 0; off < T41_DRAM_SIZE; off += 0x100000) {
		a = (volatile u32 *)(uintptr_t)(0xa0000000u + off);
		if (*a != 0xa0000000u + off) {
			t41_spl_puts("DDR: sweep fail\n");
			return -1;
		}
	}

	return 0;
}

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
	t41_spl_puts("\nT41 SPL: alive (pre-PLL)\n");

	pll_init();
	t41_spl_puts("T41 SPL: PLL configured\n");

	timer_init();
	sdram_init();

	/* Dump key DDRC + PHY registers for DDR3 debug */
	{
		volatile u32 *r;
		int j;
		/* DDRC: CFG CTRL REFCNT TIMING1-5 MMAP0 MMAP1 */
		static const struct { u32 off; const char *n; } dregs[] = {
			{0x008, "CFG"}, {0x010, "CTRL"}, {0x038, "REF"},
			{0x040, "T1"}, {0x048, "T2"}, {0x050, "T3"},
			{0x058, "T4"}, {0x060, "T5"},
			{0x078, "MM0"}, {0x080, "MM1"},
		};
		/* PHY: PLL regs + calib */
		static const struct { u32 off; const char *n; } pregs[] = {
			{0x000, "RST"}, {0x004, "MCFG"}, {0x008, "TRN"},
			{0x014, "CL"}, {0x01c, "CWL"}, {0x034, "DQW"},
			{0x140, "FBL"}, {0x144, "FBH"}, {0x148, "PDV"},
			{0x14c, "PLC"}, {0x180, "PLK"}, {0x184, "CAL"},
		};
		t41_spl_puts("DDRC:");
		for (j = 0; j < (int)(sizeof(dregs)/sizeof(dregs[0])); j++) {
			r = (volatile u32 *)(0xb34f0000 + dregs[j].off);
			t41_spl_putc(' ');
			t41_spl_puts(dregs[j].n);
			t41_spl_putc('=');
			spl_put_hex(*r);
		}
		t41_spl_puts("\nPHY:");
		for (j = 0; j < (int)(sizeof(pregs)/sizeof(pregs[0])); j++) {
			r = (volatile u32 *)(0xb3011000 + pregs[j].off);
			t41_spl_putc(' ');
			t41_spl_puts(pregs[j].n);
			t41_spl_putc('=');
			spl_put_hex(*r);
		}
		t41_spl_puts("\n");
	}

	/* Unprotect DDRC regs, rewrite REFCNT + CTRL, re-protect.
	 * HREGPRO=1 locks DDRC registers; we set it in sdram_init but
	 * the REFCNT RFC field and CKE bit get clobbered. */
	writel(0, (void __iomem *)(0xb34f0000 + 0x0d8));	/* HREGPRO=0 */
	writel(0x67aa0083, (void __iomem *)(0xb34f0000 + 0x038));
	writel(0xb092, (void __iomem *)(0xb34f0000 + 0x010));
	writel(1, (void __iomem *)(0xb34f0000 + 0x0d8));	/* HREGPRO=1 */
	{
		u32 ref = readl((void __iomem *)(0xb34f0000 + 0x038));
		u32 ctl = readl((void __iomem *)(0xb34f0000 + 0x010));
		t41_spl_puts("FIX REF="); spl_put_hex(ref);
		t41_spl_puts(" CTRL="); spl_put_hex(ctl);
		t41_spl_puts("\n");
	}

	if (dram_verify() == 0)
		t41_spl_puts("T41 SPL: DDR OK\n");
	else
		t41_spl_puts("T41 SPL: DDR verify FAILED\n");

#ifdef CONFIG_SPL_T41_USB_BOOT
	t41_spl_sfc_clk_init();
	t41_spl_puts("T41 SPL: returning to mask ROM (USB boot)\n");
	return;
#else
	t41_spl_puts("T41 SPL: loading U-Boot...\n");
	t41_spl_load_uboot();
	for (;;)
		;
#endif
}
#endif /* CONFIG_XPL_BUILD */
