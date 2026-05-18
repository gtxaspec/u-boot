// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T33 SoC SPL bring-up
 *
 * The SPL runs from on-chip SRAM with no driver model: minimal
 * console, PLLs, DDR, then load U-Boot proper. Stage 1 brings up
 * console + PLL and (CONFIG_SPL_T33_USB_BOOT) returns to the mask
 * ROM; the Innophy DDR2 (Stage 2) + SFC NOR-boot (Stage 3) land
 * next. Forward-ported from the vendor U-Boot 2022.10 PRJ spl.c
 * (PRJ008 = T33); full U-Boot uses driver model.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t33.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void t33_spl_serial_init(void);
void t33_spl_puts(const char *s);
void t33_spl_putc(char c);
void t33_spl_load_uboot(void);
void __weak sdram_init(void) { }

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t33_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t33_spl_putc(hex[(v >> i) & 0xf]);
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
	clk_ungate_uart(T33_CONSOLE_UART);
	t33_spl_serial_init();
	t33_spl_puts("\nT33 SPL: alive (pre-PLL)\n");

	socid = readl((void __iomem *)T33_SOCID_ADDR);
	t33_spl_puts("T33 SPL: SOCID ");
	spl_put_hex(socid);
	t33_spl_puts(socid == T33_SOCID ? " (T33/PRJ008)\n" : " (unexpected)\n");

	/*
	 * Vendor PRJ spl.c pre-PLL pokes: clear the OST gate bit (the
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
	t33_spl_puts("T33 SPL: PLL configured\n");

	sdram_init();

#ifdef CONFIG_SPL_T33_USB_BOOT
	/*
	 * USB-boot stage1: clocks are up. Return to the mask ROM
	 * (start.S branches in, so $ra still holds the bootrom return
	 * address and this epilogue jr's back into the bootrom USB
	 * loop). The host then uploads U-Boot proper over USB.
	 */
	t33_spl_puts("T33 SPL: returning to mask ROM (USB boot)\n");
	return;
#else
	/* Load U-Boot proper from SPI-NOR into DRAM and jump. */
	t33_spl_puts("T33 SPL: loading U-Boot...\n");
	t33_spl_load_uboot();
	for (;;)
		;
#endif
}
#endif /* CONFIG_XPL_BUILD */
