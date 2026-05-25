// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T40 PLL and clock setup (SPL)
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
#include <mach/t40.h>

/*
 * Vendor T40N clock plan (include/configs/isvp_t40.h DDR_500M /
 * APLL_912M block):
 *   APLL = 912 MHz (M=76, N=1, OD1=2, OD0=1)
 *   MPLL = 1000 MHz (M=125, N=1, OD1=3, OD0=1)  -> DDR = MPLL/2 = 500
 * Bootrom INGE descriptors program APLL=600 / MPLL=1200 for the
 * initial UART clock; pll_init() reprograms to the vendor T40N
 * operating point before sdram_init.
 */
#define T40_APLL_MNOD	((76 << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define T40_MPLL_MNOD	((125 << 20) | (1 << 14) | (3 << 11) | (1 << 8))

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
 * CPCCR: SEL_SRC=EXTAL during reprogram, then switch to APLL/MPLL.
 * Dividers: PDIV=10, H2DIV=5, H0DIV=5, L2DIV=2, CDIV=1 (matches A1
 * MPLL>=1000 band, vendor T40 plan). Source select bits 31:24 chosen
 * SEL_CPLL=1(APLL), SEL_H0/H2=2(MPLL).
 */
#define T40_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
			 ((10 - 1) << 16) | ((5 - 1) << 12) | \
			 ((5 - 1) << 8) | ((2 - 1) << 4) | ((1 - 1) << 0))

static void cpccr_init(void)
{
	u32 cpccr;

	cpccr = (cpm_readl(CPM_CPCCR) & (0xff << 24)) |
		(T40_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);

	while (cpm_readl(CPM_CPCSR) & 0xf)
		;

	cpccr = (T40_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~(0xff << 24));
	cpm_writel(cpccr, CPM_CPCCR);
}

void pll_init(void)
{
	cpm_writel((cpm_readl(CPM_CPCCR) & ~(0xff << 24)) | (0x55 << 24),
		   CPM_CPCCR);

	pll_set(CPM_CPAPCR, T40_APLL_MNOD);
	pll_set(CPM_CPMPCR, T40_MPLL_MNOD);
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
