// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T30 PLL and clock setup (SPL)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 *
 * T30 has APLL (CPU), MPLL (DDR/peripheral) and VPLL. The SPL only
 * needs APLL + MPLL for console/DDR; VPLL is left at reset and is
 * modelled by the clk-t30 DM driver in U-Boot proper. T30 uses the
 * cpm_cpxpcr_t PLL (same family as T21: M[28:20] N[19:14] OD[13:11]
 * RG[7:5], enable bit0, lock/PLL_ON bit3), NOT the T31/T23 M/N/OD1/
 * OD0 form. APLL/MPLL words are the exact vendor
 * CONFIG_SYS_{APLL,MPLL}_MNOD for the T30 profile (taken verbatim,
 * NOT recomputed). Default T30L: APLL 750 MHz, MPLL 1000 MHz
 * (DDR 500). CPCCR dividers are the vendor DDR_500M profile
 * (PCLK/H2/H0 = 8/4/4).
 */

#include <asm/io.h>
#include <mach/t30.h>

/* APLL: exact vendor CPAPCR word for the selected variant. */
#define T30_APLL_MNOD	CONFIG_T30_APLL_MNOD
/*
 * MPLL = vendor CONFIG_SYS_MPLL_MNOD for DDR_500M & CONFIG_T30:
 * (M=124,N=2,OD1=1<<11,OD0=1<<5) = 1000 MHz (DDR 500, /2, cdr=1).
 */
#define T30_MPLL_MNOD	((124 << 20) | (2 << 14) | (1 << 11) | (1 << 5))

/*
 * CPCCR: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL.
 * SEL_SRC=2 SEL_CPU=1 SEL_H0=2 SEL_H2=2
 * DIV_PCLK=8 DIV_H2=4 DIV_H0=4 DIV_L2=2 DIV_CPU=1 (vendor DDR_500M)
 */
#define T30_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
			 ((8 - 1) << 16) | ((4 - 1) << 12) | ((4 - 1) << 8) | \
			 ((2 - 1) << 4) | ((1 - 1) << 0))

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

static void cpccr_init(void)
{
	u32 cpccr;

	/* Program the dividers (low 24 bits), enable the divider write. */
	cpccr = (cpm_readl(CPM_CPCCR) & (0xff << 24)) |
		(T30_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);
	while (cpm_readl(CPM_CPCSR) & 0x7)
		;

	/* Program the clock source selects (high 8 bits). */
	cpccr = (T30_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~(0xff << 24));
	cpm_writel(cpccr, CPM_CPCCR);
}

void pll_init(void)
{
	pll_set(CPM_CPAPCR, T30_APLL_MNOD);
	pll_set(CPM_CPMPCR, T30_MPLL_MNOD);
	/* VPLL left at reset; not needed for SPL console/DDR. */
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
