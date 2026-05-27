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
