// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T21 PLL and clock setup (SPL)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 *
 * T21 has APLL (CPU), MPLL (DDR/peripheral) and VPLL. The SPL only
 * needs APLL + MPLL for console/DDR; VPLL is left at reset and is
 * modelled by the clk-t21 DM driver in U-Boot proper (no SPL/U-Boot
 * consumer drives it). APLL/MPLL words are the exact vendor
 * CONFIG_SYS_{APLL,MPLL}_MNOD for the T21 profile (non-uniform
 * M/N/OD - OD0 is in the [7:5] field, taken verbatim, NOT
 * recomputed). Default T21N: APLL 864 MHz, MPLL 900 MHz (DDR 450).
 * CPCCR dividers are the vendor DDR_450M profile (PCLK/H2/H0 =
 * 8/4/4).
 */

#include <asm/io.h>
#include <mach/t21.h>

/* APLL: exact vendor CPAPCR word for the selected variant. */
#define T21_APLL_MNOD	CONFIG_T21_APLL_MNOD
/*
 * MPLL = vendor CONFIG_SYS_MPLL_MNOD for DDR_450M & CONFIG_T21:
 * (M=74,N=1,OD1=1<<11,OD0=2<<5) = 900 MHz. The HIGH_PERF variant
 * (DDR_500M) needs a different MPLL; that is refined with the T21
 * DDR path - the SPL console does not depend on the MPLL value.
 */
#define T21_MPLL_MNOD	((74 << 20) | (1 << 14) | (1 << 11) | (2 << 5))

/*
 * CPCCR: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL.
 * SEL_SRC=2 SEL_CPU=1 SEL_H0=2 SEL_H2=2
 * DIV_PCLK=8 DIV_H2=4 DIV_H0=4 DIV_L2=2 DIV_CPU=1 (vendor DDR_450M)
 */
#define T21_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
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
		(T21_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);
	while (cpm_readl(CPM_CPCSR) & 0x7)
		;

	/* Program the clock source selects (high 8 bits). */
	cpccr = (T21_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~(0xff << 24));
	cpm_writel(cpccr, CPM_CPCCR);
}

void pll_init(void)
{
	pll_set(CPM_CPAPCR, T21_APLL_MNOD);
	pll_set(CPM_CPMPCR, T21_MPLL_MNOD);
	/* VPLL left at reset; not needed for SPL console/DDR. */
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
