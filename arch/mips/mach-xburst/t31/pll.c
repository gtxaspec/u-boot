// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 PLL and clock setup (SPL)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 *
 * T31 has three PLLs: APLL (CPU), MPLL (DDR/peripheral) and VPLL.
 * The CPCCR register selects clock sources and the core/L2/AHB/APB
 * dividers. MPLL 1200 MHz (DDR 600 MHz) and VPLL 1200 MHz are shared
 * by the whole DDR2-128M variant family; only the APLL (CPU clock)
 * differs per variant and comes from CONFIG_T31_APLL_MHZ (Kconfig
 * "T31 SoC variant" choice). T31X/N/AL/C100=1392, T31LC=1104,
 * T31L=1008; T31X reproduces the original hardcoded 1392 exactly.
 */

#include <asm/io.h>
#include <linux/build_bug.h>
#include <mach/t31.h>

/*
 * CPAPCR/CPMPCR/CPVPCR encoding: (M<<20)|(N<<14)|(OD1<<11)|(OD0<<8).
 * Pllout = 24MHz * M / (N * OD0 * OD1); Fvco = 24MHz * M / N must be
 * 1250..5000 MHz (vendor get_pllreg_value() constraints). The DDR2
 * family uses N=1, OD0=1, OD1=2 so Pllout = 12MHz * M and
 * M = CONFIG_T31_APLL_MHZ / 12 (must divide evenly).
 */
#define T31_APLL_M	(CONFIG_T31_APLL_MHZ / 12)
#define T31_APLL_MNOD	((T31_APLL_M << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define T31_MPLL_M	(CONFIG_T31_MPLL_MHZ / 12)
#define T31_MPLL_MNOD	((T31_MPLL_M << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define T31_VPLL_MNOD	((100 << 20) | (1 << 14) | (2 << 11) | (1 << 8))

static_assert(CONFIG_T31_APLL_MHZ % 12 == 0,
	      "T31_APLL_MHZ must be a multiple of 12");
static_assert(T31_APLL_M >= 16 && T31_APLL_M <= 2500,
	      "T31 APLL M out of range");
static_assert(24 * T31_APLL_M >= 1250 && 24 * T31_APLL_M <= 5000,
	      "T31 APLL Fvco (24*M) out of 1250-5000 MHz");
static_assert(CONFIG_T31_MPLL_MHZ % 12 == 0,
	      "T31_MPLL_MHZ must be a multiple of 12");
static_assert(24 * T31_MPLL_M >= 1250 && 24 * T31_MPLL_M <= 5000,
	      "T31 MPLL Fvco (24*M) out of 1250-5000 MHz");

/*
 * CPCCR: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL. SEL_SRC=2 SEL_CPU=1
 * SEL_H0=2 SEL_H2=2; DIV_L2=2 DIV_CPU=1. The H0/H2/PCLK dividers
 * track the MPLL band (vendor DDR_xxxM table): MPLL 1008 (DDR
 * 504) -> PCLK 8 / H2 4 / H0 4; MPLL 1200/1500 (DDR 600/750) ->
 * PCLK 12 / H2 6 / H0 6 - keeps the bus clocks in spec as MPLL
 * changes.
 */
#if CONFIG_T31_MPLL_MHZ <= 1008
#define T31_CPCCR_DIV_PCLK	8
#define T31_CPCCR_DIV_H2	4
#define T31_CPCCR_DIV_H0	4
#else
#define T31_CPCCR_DIV_PCLK	12
#define T31_CPCCR_DIV_H2	6
#define T31_CPCCR_DIV_H0	6
#endif
#define T31_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
			 ((T31_CPCCR_DIV_PCLK - 1) << 16) | \
			 ((T31_CPCCR_DIV_H2 - 1) << 12) | \
			 ((T31_CPCCR_DIV_H0 - 1) << 8) | \
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
