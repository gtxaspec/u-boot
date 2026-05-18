// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T33 PLL and clock setup (SPL)
 *
 * Forward-port of the vendor U-Boot 2022.10 PRJ pllsetting.c for the
 * T33 (PRJ008) "L" profile: APLL 950 MHz, MPLL 1300 MHz, DDR 650,
 * EXTAL 24 MHz. T33 uses the M/N/OD0/OD1 CPAPCR/CPMPCR form (like
 * T31/T23/T20). The register words are the exact host
 * pll_params_creator output for PRJ008_l_goat_sfcnor (verbatim, NOT
 * recomputed):
 *   APLL  M=0x1db N=3 OD1=4 OD0=1 EN=1 -> CPAPCR 0x1db0e1c1 (950 MHz)
 *   MPLL  M=0x145 N=2 OD1=3 OD0=1 EN=1 -> CPMPCR 0x145099c1 (1300 MHz)
 * CPxPCR = (EN<<0)|(M<<20)|(N<<14)|(OD1<<11)|(OD0<<8)|(1<<7)|(1<<6).
 * T33 (PRJ008) does NOT program VPLL (vendor pll_sets() skips it).
 * CPCCR dividers CDIV=0 L2DIV=1 H0DIV=4 H2DIV=4 PDIV=9; selects
 * SRC=2 CPLL=1 H0PLL=2 H2PLL=2.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t33.h>

/* Exact vendor pll_params_creator words for PRJ008_l (T33). */
#define T33_CPAPCR	0x1db0e1c1u	/* APLL 950 MHz */
#define T33_CPMPCR	0x145099c1u	/* MPLL 1300 MHz */
/*
 * CPCCR: phase 1 = base | dividers; phase 2 = source selects |
 * dividers. Vendor cpccr_default writes 0x55700000 first.
 */
#define T33_CPCCR_DEFAULT	0x55700000u
#define T33_CPCCR_DIV		0x55794410u	/* | CDIV/L2/H0/H2/PDIV */
#define T33_CPCCR_SEL		0x9a094410u	/* SEL_* | dividers */

static u32 cpm_r(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void cpm_w(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

/*
 * Vendor pll_set(): clear the enable bit, write the full M/N/OD/EN
 * word, then poll the lock bit (PLL_PLLON, bit 3).
 */
static void pll_set(unsigned int reg, u32 word)
{
	cpm_w(cpm_r(reg) & ~PLL_PLLEN, reg);
	cpm_w(word, reg);
	while (!(cpm_r(reg) & PLL_PLLON))
		;
}

void pll_init(void)
{
	/* cpccr_default: known state, wait CPCSR stable. */
	cpm_w(T33_CPCCR_DEFAULT, CPM_CPCCR);
	while ((cpm_r(CPM_CPCSR) & 0xf0000007) != 0xf0000000)
		;

	pll_set(CPM_CPAPCR, T33_CPAPCR);	/* APLL 950 MHz */
	pll_set(CPM_CPMPCR, T33_CPMPCR);	/* MPLL 1300 MHz */
	/* T33 (PRJ008) does not use VPLL - skipped, as the vendor does. */

	/* cpccr_sets: program dividers, then the source selects. */
	cpm_w(T33_CPCCR_DIV, CPM_CPCCR);
	while (cpm_r(CPM_CPCSR) & 7)
		;
	cpm_w(T33_CPCCR_SEL, CPM_CPCCR);
	while ((cpm_r(CPM_CPCSR) & 0xf0000000) != 0xf0000000)
		;
}

/* Ungate the console UART (CLKGR0: UART0 = bit 14, UART1 = bit 15). */
void clk_ungate_uart(unsigned int idx)
{
	cpm_w(cpm_r(CPM_CLKGR0) & ~(CPM_CLKGR0_UART0 << idx), CPM_CLKGR0);
}
