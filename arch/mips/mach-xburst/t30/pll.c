// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T30 PLL and clock setup (SPL)
 *
 * T30 has APLL (CPU), MPLL (DDR/peripheral) and VPLL; the SPL only
 * needs APLL + MPLL (VPLL is left at reset and is modelled by the
 * clk-t30 DM driver in U-Boot proper). The per-SKU APLL/MPLL setpoints
 * (exact vendor CONFIG_SYS_*_MNOD words, non-uniform M/N/OD - OD0 in
 * the [7:5] field, taken verbatim, NOT recomputed) and the CPCCR
 * divider word live in the DDR variant struct (drivers/ram/ingenic/
 * ddr_t30_types.c) and are selected at runtime by matching the &ddr
 * node's per-SKU compatible (ingenic,t30<sku>-ddr-innophy) - the same
 * of_match table the RAM driver uses. soc.c calls fdtdec_setup()
 * before pll_init() so the FDT blob is available here, before driver
 * model is up. T30 reuses the shared XBurst1 DDR driver (ddr_t31.c),
 * hence the ingenic_t31_ddr_pll_setpoints() helper name.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <hang.h>
#include <mach/t30.h>

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
	/* VPLL left at reset; not needed for SPL console/DDR. */
	cpccr_init(cpccr);
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
