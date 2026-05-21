// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 PLL and clock setup (SPL)
 *
 * A1 (XBurst2) has four PLLs: APLL (CPU), MPLL (DDR/peripheral),
 * VPLL and EPLL. The CPCCR register selects clock sources and the
 * core/L2/AHB0/AHB2/APB dividers; AHB1CDR is a separate register.
 *
 * A1N: APLL 1104 / MPLL 1608 (DDR 804) / VPLL 1200 / EPLL 1500
 * CPU from APLL, DDR from MPLL.
 *
 * PLL encoding: (M<<20)|(N<<14)|(OD1<<11)|(OD0<<8)
 * Pllout = 24MHz * M / (N * OD0 * OD1)
 * All A1 variants use N=1, OD0=1, OD1=2 -> Pllout = 12MHz * M
 */

#include <asm/io.h>
#include <linux/build_bug.h>
#include <mach/a1.h>

#define A1_APLL_M	(CONFIG_A1_APLL_MHZ / 12)
#define A1_APLL_MNOD	((A1_APLL_M << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define A1_MPLL_M	(CONFIG_A1_MPLL_MHZ / 12)
#define A1_MPLL_MNOD	((A1_MPLL_M << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define A1_VPLL_MNOD	((100 << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define A1_EPLL_MNOD	((125 << 20) | (1 << 14) | (2 << 11) | (1 << 8))

static_assert(CONFIG_A1_APLL_MHZ % 12 == 0,
	      "A1_APLL_MHZ must be a multiple of 12");
static_assert(A1_APLL_M >= 16 && A1_APLL_M <= 2500,
	      "A1 APLL M out of range");
static_assert(CONFIG_A1_MPLL_MHZ % 12 == 0,
	      "A1_MPLL_MHZ must be a multiple of 12");
static_assert(24 * A1_MPLL_M >= 1250 && 24 * A1_MPLL_M <= 5000,
	      "A1 MPLL Fvco (24*M) out of 1250-5000 MHz");

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
 * CPCCR: SEL_SRC=2(EXTAL), SEL_CPLL=1(APLL), SEL_H0CLK=2(MPLL),
 *        SEL_H2CLK=2(MPLL).
 * Dividers from vendor (MPLL >= 1400 MHz band):
 *   PDIV=10, H2DIV=5, H0DIV=5, L2DIV=2, CDIV=1
 */
#define A1_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
			 ((10 - 1) << 16) | \
			 ((5 - 1) << 12) | \
			 ((5 - 1) << 8) | \
			 ((2 - 1) << 4) | ((1 - 1) << 0))

/*
 * AHB1CDR: SEL_AHB1 = MPLL (bits 1:0 = 0x2), DIV (bits 7:4).
 * Vendor: AHB1 = MPLL/5 with CE bit.
 */
#define A1_AHB1CDR_CFG	((1 << 8) | ((5 - 1) << 4) | 0x2)

static void cpccr_init(void)
{
	u32 cpccr, ahb1;

	/* Program dividers (low 24 bits), enable divider write bits 22:20 */
	cpccr = (cpm_readl(CPM_CPCCR) & (0xff << 24)) |
		(A1_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);

	/* AHB1CDR: program div+sel, set CE */
	ahb1 = (cpm_readl(CPM_AHB1CDR) & 0x3) |
	       (A1_AHB1CDR_CFG & ~0x3) | (1 << 8);
	cpm_writel(ahb1, CPM_AHB1CDR);

	while (cpm_readl(CPM_CPCSR) & 0xf)
		;

	/* Program clock source selects (high 8 bits) */
	cpccr = (A1_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~(0xff << 24));
	cpm_writel(cpccr, CPM_CPCCR);

	/* AHB1CDR source select */
	ahb1 = (A1_AHB1CDR_CFG & 0x3) |
	       (cpm_readl(CPM_AHB1CDR) & ~0x3);
	cpm_writel(ahb1, CPM_AHB1CDR);
}

void pll_init(void)
{
	/* Set bus select to EXTAL before reprogramming PLLs */
	cpm_writel((cpm_readl(CPM_CPCCR) & ~(0xff << 24)) | (0x55 << 24),
		   CPM_CPCCR);

	pll_set(CPM_CPAPCR, A1_APLL_MNOD);
	pll_set(CPM_CPMPCR, A1_MPLL_MNOD);
	pll_set(CPM_CPVPCR, A1_VPLL_MNOD);
	pll_set(CPM_CPEPCR, A1_EPLL_MNOD);
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
