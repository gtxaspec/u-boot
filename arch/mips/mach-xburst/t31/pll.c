// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 PLL and clock setup (SPL)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 *
 * T31 has three PLLs: APLL (CPU), MPLL (DDR/peripheral) and VPLL. The CPCCR
 * register selects the clock sources and the core/L2/AHB/APB dividers. The
 * per-SKU APLL/MPLL setpoints and the CPCCR divider word are elements [0..2] of
 * the &ddr node's "ingenic,sdram-params" devicetree array; the UCLASS_RAM probe
 * (drivers/ram/ingenic/ddr_t31.c) reads them and calls pll_init_params() before
 * bringing DDR up, in the first loader stage. VPLL is fixed at 1200 MHz on
 * every variant. Uses the T31/T23-style M/N/OD1/OD0 PLL encoding.
 */

#include <asm/io.h>
#include <mach/t31.h>

/* CPVPCR encoding: (M<<20)|(N<<14)|(OD1<<11)|(OD0<<8). VPLL 1200 = 12*100. */
#define T31_VPLL_MNOD	((100 << 20) | (1 << 14) | (2 << 11) | (1 << 8))

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

static void cpccr_init(u32 cpccr_cfg)
{
	u32 cpccr;

	/* Program the dividers (low 24 bits), enable the divider write. */
	cpccr = (cpm_readl(CPM_CPCCR) & (0xff << 24)) |
		(cpccr_cfg & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);
	while (cpm_readl(CPM_CPCSR) & 0x7)
		;

	/* Program the clock source selects (high 8 bits). */
	cpccr = (cpccr_cfg & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~(0xff << 24));
	cpm_writel(cpccr, CPM_CPCCR);
}

/*
 * Bring up APLL/MPLL/VPLL + the CPCCR dividers from explicit setpoints. Called
 * from the UCLASS_RAM probe (DT params) before DDR is brought up.
 */
void pll_init_params(u32 apll, u32 mpll, u32 cpccr)
{
	pll_set(CPM_CPAPCR, apll);
	pll_set(CPM_CPMPCR, mpll);
	pll_set(CPM_CPVPCR, T31_VPLL_MNOD);
	cpccr_init(cpccr);
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
