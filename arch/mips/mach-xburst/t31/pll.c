// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 PLL and clock setup (SPL)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 *
 * T31 has three PLLs: APLL (CPU), MPLL (DDR/peripheral) and VPLL.
 * The CPCCR register selects clock sources and the core/L2/AHB/APB
 * dividers. The per-SKU APLL/MPLL setpoints and the CPCCR divider word
 * live in the DDR variant struct (drivers/ram/ingenic/ddr_t31_types.c)
 * and are selected at runtime from the DT node's compatible. soc.c calls
 * fdtdec_setup() before pll_init() so the FDT blob is available here,
 * before driver model is up. VPLL is fixed at 1200 MHz on every variant.
 */

#include <asm/io.h>
#include <hang.h>
#include <mach/t31.h>

/* CPVPCR encoding: (M<<20)|(N<<14)|(OD1<<11)|(OD0<<8). VPLL 1200 = 12*100. */
#define T31_VPLL_MNOD	((100 << 20) | (1 << 14) | (2 << 11) | (1 << 8))

/*
 * SPL helper from the T31 DDR driver: find the DDR node in the FDT (by
 * its per-SKU compatible) and return that SKU's APLL/MPLL setpoints and
 * the CPCCR divider word. Runs before driver model is up.
 */
int ingenic_t31_ddr_pll_setpoints(u32 *apll_mnod, u32 *mpll_mnod, u32 *cpccr);

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

void pll_init(void)
{
	u32 apll, mpll, cpccr;

	if (ingenic_t31_ddr_pll_setpoints(&apll, &mpll, &cpccr))
		hang();

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
