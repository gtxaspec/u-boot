// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T10 PLL and clock setup (SPL)
 *
 * T10 has APLL (CPU), MPLL (DDR/peripheral) and VPLL; the SPL only needs
 * APLL + MPLL (VPLL is left at reset and is modelled by the clk-t10 DM driver
 * in U-Boot proper). T10 uses the T31/T23-style M/N/OD1/OD0 PLL encoding (no
 * PLLRG; write mnod | enable, poll PLL_ON). The per-SKU APLL/MPLL setpoints
 * (exact vendor CONFIG_SYS_*_MNOD words, taken verbatim) and the CPCCR divider
 * word are elements [0..2] of the &ddr node's "ingenic,sdram-params" array; the
 * UCLASS_RAM probe (drivers/ram/ingenic/ddr_t20.c) reads them and calls
 * pll_init_params() before bringing DDR up, in the first loader stage. Default
 * T10N: APLL 860, MPLL 1200, DDR 400 (MPLL/3); CPCCR is the vendor T10 DDR_400M
 * profile (PCLK/H2/H0 = 12/6/6).
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t10.h>

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
 * is brought up. VPLL is left at reset (not needed for the SPL console/DDR).
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
