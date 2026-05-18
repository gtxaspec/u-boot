// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T33 PLL and clock setup (SPL)
 *
 * Forward-port of the vendor U-Boot 2022.10 PRJ pllsetting.c. T33
 * uses the M/N/OD0/OD1 CPAPCR/CPMPCR form (like T31/T23/T20),
 * EXTAL 24 MHz, and does NOT program VPLL (vendor pll_sets() skips
 * it). Two DDR2 bins, CONFIG_T33_DDR2_550-selected; words are the
 * exact host pll_params_creator output (verbatim, NOT recomputed):
 *   L/DL  (PRJ008_l)  APLL 0x1db0e1c1 950 / MPLL 0x145099c1 1300
 *                     / DDR 650; H0/H2DIV 4 PDIV 9
 *   VL/ZL (PRJ008_vl) APLL 0x1290a1c1 891 / MPLL 0x113099c1 1100
 *                     / DDR 550; H0/H2DIV 3 PDIV 7
 * CPxPCR = (EN<<0)|(M<<20)|(N<<14)|(OD1<<11)|(OD0<<8)|(1<<7)|(1<<6).
 * CPCCR selects (bin-invariant) SRC=2 CPLL=1 H0PLL=2 H2PLL=2.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t33.h>

/*
 * Exact vendor pll_params_creator words, per DDR2 bin (verbatim,
 * NOT recomputed). L/DL (PRJ008_l) = APLL 950 / MPLL 1300 / DDR
 * 650; VL/ZL (PRJ008_vl, CONFIG_T33_DDR2_550) = APLL 891 / MPLL
 * 1100 / DDR 550. CPCCR phase 1 = base | dividers; phase 2 =
 * source selects | dividers; vendor cpccr_default writes
 * 0x55700000 first (bin-invariant).
 */
#define T33_CPCCR_DEFAULT	0x55700000u
#if defined(CONFIG_T33_DDR2_550)
#define T33_CPAPCR	0x1290a1c1u	/* APLL 891 MHz (VL/ZL) */
#define T33_CPMPCR	0x113099c1u	/* MPLL 1100 MHz */
#define T33_CPCCR_DIV	0x55773310u	/* H0/H2DIV 3, PDIV 7 */
#define T33_CPCCR_SEL	0x9a073310u
#else
#define T33_CPAPCR	0x1db0e1c1u	/* APLL 950 MHz (L/DL) */
#define T33_CPMPCR	0x145099c1u	/* MPLL 1300 MHz */
#define T33_CPCCR_DIV	0x55794410u	/* H0/H2DIV 4, PDIV 9 */
#define T33_CPCCR_SEL	0x9a094410u
#endif

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

/*
 * DDR clock. Vendor sdram_init() does _clk_set_rate(DDR,
 * CONFIG_SYS_MEM_FREQ / 2); for PRJ008_l that is the MPLL-sourced
 * DDR CK of 325 MHz (650 MT/s). CPM_DDRCDR layout (vendor CGU
 * descriptor [DDR] = {.., sel_bit 30, ce 29, busy 28, stop 27}):
 * src[31:30] (1=APLL, 2=MPLL), ce[29], busy[28], stop[27],
 * divider[7:0]. cdr = MPLL / CK - 1.
 */
#define T33_EXTAL_HZ	24000000U
#if defined(CONFIG_T33_DDR2_550)
#define T33_DDR_CK_HZ	275000000U	/* VL/ZL: 550M / 2 */
#else
#define T33_DDR_CK_HZ	325000000U	/* L/DL: 650M / 2 */
#endif

/* CPxPCR (CPAPCR/CPMPCR) -> Hz. Shared by the DDR and SFC0 clocks. */
u32 t33_pll_rate(unsigned int cpxpcr_off)
{
	u32 v = cpm_r(cpxpcr_off);
	u32 m = (v >> 20) & 0xfff;
	u32 n = (v >> 14) & 0x3f;
	u32 od1 = (v >> 11) & 0x7;
	u32 od0 = (v >> 8) & 0x7;

	/* Guard the divisors (defensive - the words are fixed nonzero
	 * constants, but never divide by a stray 0 field). */
	if (!n)
		n = 1;
	if (!od1)
		od1 = 1;
	if (!od0)
		od0 = 1;

	return (u32)((u64)T33_EXTAL_HZ * m / n / od1 / od0);
}

void ddr_clk_init(void)
{
	u32 mpll = t33_pll_rate(CPM_CPMPCR);
	u32 cdr = ((mpll + T33_DDR_CK_HZ - 1) / T33_DDR_CK_HZ - 1) & 0xff;
	u32 v;

	v = cpm_r(CPM_DDRCDR);
	v &= ~((3u << 30) | (1u << 28) | (1u << 27) | 0xff);
	v |= (2u << 30) | (1u << 29) | cdr;	/* src=MPLL, ce, divider */
	cpm_w(v, CPM_DDRCDR);
	while (cpm_r(CPM_DDRCDR) & (1u << 28))	/* wait change-busy clear */
		;
}
