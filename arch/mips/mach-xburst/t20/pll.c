// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T20 PLL and clock setup (SPL)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 *
 * T20 has APLL (CPU), MPLL (DDR/peripheral) and VPLL. The SPL only
 * needs APLL + MPLL for console/DDR; VPLL is left at reset and is
 * modelled by the clk-t20 DM driver in U-Boot proper. T20 uses the
 * T31/T23-style M/N/OD1/OD0 PLL encoding (T20 cpm_cpxpcr_t: PLLM
 * [31:20] PLLN[19:14] PLLOD1[13:11] PLLOD0[10:8], enable bit0,
 * lock/PLL_ON bit3 - NO PLLRG, unlike T21/T30). The write/poll
 * sequence is the same as every XBurst1 PLL (write mnod | enable,
 * poll PLL_ON); only the clk-t20 DM rate decode is the M/N/OD1/OD0
 * form (mirrors clk-t31). APLL/MPLL words are the exact vendor
 * CONFIG_SYS_{APLL,MPLL}_MNOD (verbatim, NOT recomputed). Default
 * T20N: APLL 860 MHz, MPLL 1000 MHz (DDR 500). CPCCR dividers are
 * the vendor DDR_500M profile (PCLK/H2/H0 = 8/4/4).
 */

#include <asm/io.h>
#include <mach/t20.h>

/* APLL: exact vendor CPAPCR word for the selected variant. */
#define T20_APLL_MNOD	CONFIG_T20_APLL_MNOD
/*
 * MPLL = vendor CONFIG_SYS_MPLL_MNOD for DDR_500M & CONFIG_T20:
 * (M=125,N=3,OD1=1<<11,OD0=1<<8) = 1000 MHz (DDR 500, /2, cdr=1).
 */
#define T20_MPLL_MNOD	((125 << 20) | (3 << 14) | (1 << 11) | (1 << 8))

/*
 * CPCCR: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL.
 * SEL_SRC=2 SEL_CPU=1 SEL_H0=2 SEL_H2=2
 * DIV_PCLK=8 DIV_H2=4 DIV_H0=4 DIV_L2=2 DIV_CPU=1 (vendor DDR_500M)
 */
#define T20_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
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
		(T20_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);
	while (cpm_readl(CPM_CPCSR) & 0x7)
		;

	/* Program the clock source selects (high 8 bits). */
	cpccr = (T20_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~(0xff << 24));
	cpm_writel(cpccr, CPM_CPCCR);
}

void pll_init(void)
{
	pll_set(CPM_CPAPCR, T20_APLL_MNOD);
	pll_set(CPM_CPMPCR, T20_MPLL_MNOD);
	/* VPLL left at reset; not needed for SPL console/DDR. */
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
