// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T10 PLL and clock setup (SPL)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 *
 * T10 has APLL (CPU), MPLL (DDR/peripheral) and VPLL. The SPL only
 * needs APLL + MPLL for console/DDR; VPLL is left at reset and is
 * modelled by the clk-t10 DM driver in U-Boot proper. T10 uses the
 * T31/T23-style M/N/OD1/OD0 PLL encoding (T10 cpm_cpxpcr_t: PLLM
 * [31:20] PLLN[19:14] PLLOD1[13:11] PLLOD0[10:8], enable bit0,
 * lock/PLL_ON bit3 - NO PLLRG, unlike T21/T30). The write/poll
 * sequence is the same as every XBurst1 PLL (write mnod | enable,
 * poll PLL_ON); only the clk-t10 DM rate decode is the M/N/OD1/OD0
 * form (mirrors clk-t31). APLL/MPLL words are the exact vendor
 * CONFIG_SYS_{APLL,MPLL}_MNOD (verbatim, NOT recomputed). Default
 * T10N: APLL 860 MHz (same word as T20N), MPLL 1200 MHz, DDR 400
 * (MEM_FREQ = MPLL/3). CPCCR dividers are the vendor T10 DDR_400M
 * profile (PCLK/H2/H0 = 12/6/6). HW-validate the divider profile
 * when a T10 board is available (sketch ported without silicon).
 */

#include <asm/io.h>
#include <mach/t10.h>

/* APLL: exact vendor CPAPCR word for the selected variant. */
#define T10_APLL_MNOD	CONFIG_T10_APLL_MNOD
/*
 * MPLL = vendor CONFIG_SYS_MPLL_MNOD for DDR_400M & CONFIG_T10:
 * (M=100,N=1,OD1=2<<11,OD0=1<<8) = 1200 MHz (DDR 400 = MPLL/3).
 */
#define T10_MPLL_MNOD	((100 << 20) | (1 << 14) | (2 << 11) | (1 << 8))

/*
 * CPCCR: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL.
 * SEL_SRC=2 SEL_CPU=1 SEL_H0=2 SEL_H2=2
 * DIV_PCLK=12 DIV_H2=6 DIV_H0=6 DIV_L2=2 DIV_CPU=1 (vendor T10
 * DDR_400M profile, isvp_common.h).
 */
#define T10_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
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
		(T10_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);
	while (cpm_readl(CPM_CPCSR) & 0x7)
		;

	/* Program the clock source selects (high 8 bits). */
	cpccr = (T10_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~(0xff << 24));
	cpm_writel(cpccr, CPM_CPCCR);
}

void pll_init(void)
{
	pll_set(CPM_CPAPCR, T10_APLL_MNOD);
	pll_set(CPM_CPMPCR, T10_MPLL_MNOD);
	/* VPLL left at reset; not needed for SPL console/DDR. */
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
