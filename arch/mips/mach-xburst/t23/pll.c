// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T23 PLL and clock setup (SPL)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 *
 * T23 has APLL (CPU) and MPLL (DDR/peripheral); no VPLL is used.
 * The APLL CPAPCR word is the exact vendor CONFIG_SYS_APLL_MNOD for
 * the selected T23 SoC-variant (Kconfig "T23 SoC variant" choice ->
 * CONFIG_T23_APLL_MNOD; non-uniform M/N/OD, taken verbatim - not
 * recomputed). MPLL is the standard 1200 MHz word, same as T31.
 * CPCCR mirrors the T31 divider profile for now; the DDR-clock
 * specific dividers are refined when the T23 DDR path lands.
 */

#include <asm/io.h>
#include <mach/t23.h>

/* APLL: exact vendor CPAPCR word for the selected variant. */
#define T23_APLL_MNOD	CONFIG_T23_APLL_MNOD
/* MPLL: exact vendor CPMPCR word, per variant (1200 std /
 * 1000 for the T23X/T23DN DDR_500M profile). */
#define T23_MPLL_MNOD	CONFIG_T23_MPLL_MNOD

/*
 * CPCCR: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL. SEL_SRC=2 SEL_CPU=1
 * SEL_H0=2 SEL_H2=2; DIV_L2=2 DIV_CPU=1. Bus dividers track the
 * MPLL band (vendor DDR_xxxM table): the T23X/T23DN MPLL-1000
 * (DDR 500) profile uses PCLK 8 / H2 4 / H0 4; the MPLL-1200
 * variants (DDR 600/400) use PCLK 12 / H2 6 / H0 6.
 */
#if CONFIG_T23_DDR_MHZ == 500
#define T23_CPCCR_DIV_PCLK	8
#define T23_CPCCR_DIV_HX	4
#else
#define T23_CPCCR_DIV_PCLK	12
#define T23_CPCCR_DIV_HX	6
#endif
#define T23_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
			 ((T23_CPCCR_DIV_PCLK - 1) << 16) | \
			 ((T23_CPCCR_DIV_HX - 1) << 12) | \
			 ((T23_CPCCR_DIV_HX - 1) << 8) | \
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
		(T23_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);
	while (cpm_readl(CPM_CPCSR) & 0x7)
		;

	/* Program the clock source selects (high 8 bits). */
	cpccr = (T23_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~(0xff << 24));
	cpm_writel(cpccr, CPM_CPCCR);
}

void pll_init(void)
{
	pll_set(CPM_CPAPCR, T23_APLL_MNOD);
	pll_set(CPM_CPMPCR, T23_MPLL_MNOD);
	/* T23 has no VPLL; do not touch CPM_CPVPCR. */
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
