// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T41 PLL and clock setup (SPL)
 *
 * T40 (XBurst2) has four PLLs: APLL (CPU), MPLL (DDR/peripheral),
 * EPLL, and VPLL. CPM_CPCCR selects clock sources and the core
 * dividers.
 *
 * Default T40: APLL 1404 MHz / MPLL 1000 MHz (DDR 500). PLL encoding
 * matches A1: (M<<20)|(N<<14)|(OD1<<11)|(OD0<<8).
 *
 * TODO: this is a faithful transliteration of the vendor SPL clk path
 * boiled down to the bring-up minimum. Real-silicon validation will
 * surface deltas (the vendor SPL has a switch-table indexed by EFUSE
 * subsoctype to pick the variant clock plan); for now T40-DDR2-500 is
 * the only target.
 */

#include <asm/io.h>
#include <mach/t41.h>
#include <mach/t41-ddr.h>		/* T41_APLL_MNOD / T41_MPLL_MNOD */

/*
 * APLL and MPLL M/N/OD values come from the per-variant header
 * (<mach/t41n-ddr.h> or <mach/t41nq-ddr.h>) - vendor isvp_t40.h
 * has different operating points for T40N (APLL=912/MPLL=1000) and
 * T40XP (APLL=1008/MPLL=1200). Bootrom INGE descriptors program
 * APLL=600/MPLL=1200 for the initial UART clock; pll_init()
 * reprograms to the per-variant production operating point before
 * sdram_init.
 */

static void cpm_writel(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

static u32 cpm_readl(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void pll_set(unsigned int reg, u32 mnod)
{
	cpm_writel(mnod | PLL_PLLEN, reg);
	while (!(cpm_readl(reg) & PLL_PLLON))
		;
}

/*
 * CPCCR vendor T40N production values per include/configs/isvp_t40.h
 * DDR_500M block: PDIV=12, H2DIV=6, H0DIV=6, L2DIV=2, CDIV=1. SEL
 * bits chosen for APLL=CPU clock + MPLL=H0/H2/peripherals. Encoded
 * value = 0x9a0b5510 (matches vendor T40 INGE descriptor SEL value).
 */
#define T41_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
			 ((12 - 1) << 16) | ((6 - 1) << 12) | \
			 ((6 - 1) << 8) | ((2 - 1) << 4) | ((1 - 1) << 0))

static void cpccr_init(void)
{
	u32 cpccr;

	cpccr = (cpm_readl(CPM_CPCCR) & (0xff << 24)) |
		(T41_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);

	while (cpm_readl(CPM_CPCSR) & 0xf)
		;

	cpccr = (T41_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~(0xff << 24));
	cpm_writel(cpccr, CPM_CPCCR);
}

void pll_init(void)
{
	cpm_writel((cpm_readl(CPM_CPCCR) & ~(0xff << 24)) | (0x55 << 24),
		   CPM_CPCCR);

	pll_set(CPM_CPAPCR, T41_APLL_MNOD);
	pll_set(CPM_CPMPCR, T41_MPLL_MNOD);
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
