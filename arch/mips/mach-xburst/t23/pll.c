// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T23 PLL and clock setup (SPL)
 *
 * T23 has APLL (CPU) and MPLL (DDR/peripheral); no VPLL. The per-SKU APLL/MPLL
 * setpoints (exact vendor CONFIG_SYS_*_MNOD words, non-uniform N/OD) and the
 * CPCCR divider word are elements [0..2] of the &ddr node's
 * "ingenic,sdram-params" array; T23's imperative board_init_f bring-up
 * (drivers/ram/ingenic/ddr_t31.c) reads them and calls pll_init_params() before
 * driver model is up. Uses the T31/T23-style M/N/OD1/OD0 PLL encoding.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t23.h>

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
 * Bring up APLL/MPLL + the CPCCR dividers from explicit setpoints, before DDR
 * is brought up. T23 has no VPLL, so CPM_CPVPCR is left untouched.
 */
void pll_init_params(u32 apll, u32 mpll, u32 cpccr)
{
	pll_set(CPM_CPAPCR, apll);
	pll_set(CPM_CPMPCR, mpll);
	cpccr_init(cpccr);
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
