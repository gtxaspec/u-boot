// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 PLL and clock setup (SPL)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 *
 * T31 has three PLLs: APLL (CPU), MPLL (DDR/peripheral) and VPLL.
 * The CPCCR register selects clock sources and the core/L2/AHB/APB
 * dividers. Values below are the isvp_t31_sfcnor_ddr128M profile
 * (DDR2 128 MB): APLL 1392 MHz, MPLL 1200 MHz (DDR 600 MHz),
 * VPLL 1200 MHz.
 *
 * TODO: parameterise PLL/divider values via Kconfig/device tree per
 * board (T31A/T31LC/lite have different APLL/DDR targets).
 */

#include <asm/io.h>
#include <mach/t31.h>

/* PLL M/N/OD encodings: (M<<20)|(N<<14)|(OD1<<11)|(OD0<<8) */
#define T31_APLL_MNOD	((116 << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define T31_MPLL_MNOD	((100 << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define T31_VPLL_MNOD	((100 << 20) | (1 << 14) | (2 << 11) | (1 << 8))

/*
 * CPCCR: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL, dividers for DDR 600.
 * SEL_SRC=2 SEL_CPU=1 SEL_H0=2 SEL_H2=2
 * DIV_PCLK=12 DIV_H2=6 DIV_H0=6 DIV_L2=2 DIV_CPU=1
 */
#define T31_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
			 ((12 - 1) << 16) | ((6 - 1) << 12) | ((6 - 1) << 8) | \
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
		(T31_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);
	while (cpm_readl(CPM_CPCSR) & 0x7)
		;

	/* Program the clock source selects (high 8 bits). */
	cpccr = (T31_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~(0xff << 24));
	cpm_writel(cpccr, CPM_CPCCR);
}

void pll_init(void)
{
	pll_set(CPM_CPAPCR, T31_APLL_MNOD);
	pll_set(CPM_CPMPCR, T31_MPLL_MNOD);
	pll_set(CPM_CPVPCR, T31_VPLL_MNOD);
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
