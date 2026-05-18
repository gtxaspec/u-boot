// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 PLL and clock setup (SPL)
 *
 * Forward-port of the vendor U-Boot 2022.10 PRJ pllsetting.c for the
 * T32 (PRJ007) "lq" profile: APLL 900 MHz, MPLL 1200 MHz, VPLL
 * 1188 MHz, DDR 600, EXTAL 24 MHz. T32 uses the M/N/OD0/OD1
 * CPAPCR/CPMPCR/CPVPCR form (like T31/T23/T20). The register words
 * are the exact host pll_params_creator output for
 * PRJ007_lq_goat_sfcnor (verbatim, NOT recomputed):
 *   APLL M=0x4b N=1 OD1=2 OD0=1 EN=1 -> CPAPCR 0x04b051c1 (900 MHz)
 *   MPLL M=0x64 N=1 OD1=2 OD0=1 EN=1 -> CPMPCR 0x064051c1 (1200 MHz)
 *   VPLL M=0x63 N=1 OD1=2 OD0=1 EN=1 -> CPVPCR 0x063051c1 (1188 MHz)
 * CPxPCR = (EN<<0)|(M<<20)|(N<<14)|(OD1<<11)|(OD0<<8)|(1<<7)|(1<<6).
 * UNLIKE T33/PRJ008, T32/PRJ007 DOES program VPLL (vendor pll_sets()
 * skips it only for PRJ008). CPCCR dividers CDIV=0 L2DIV=1 H0DIV=3
 * H2DIV=3 PDIV=7; selects SRC=2 CPLL=1 H0PLL=2 H2PLL=2 (same mux as
 * T33 - source-select is not frequency dependent).
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t32.h>

/* Exact vendor pll_params_creator words for PRJ007_lq (T32). */
#define T32_CPAPCR	0x04b051c1u	/* APLL 900 MHz */
#define T32_CPMPCR	0x064051c1u	/* MPLL 1200 MHz */
#define T32_CPVPCR	0x063051c1u	/* VPLL 1188 MHz */
/*
 * CPCCR: phase 1 = base | dividers; phase 2 = source selects |
 * dividers. Vendor cpccr_default writes 0x55700000 first. Divider
 * field = (CDIV<<0)|(L2DIV<<4)|(H0DIV<<8)|(H2DIV<<12)|(PDIV<<16) =
 * 0x73310 for PRJ007_lq; SEL bits 0x9a000000 (same mux as T33).
 */
#define T32_CPCCR_DEFAULT	0x55700000u
#define T32_CPCCR_DIV		0x55773310u	/* | CDIV/L2/H0/H2/PDIV */
#define T32_CPCCR_SEL		0x9a073310u	/* SEL_* | dividers */

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
	cpm_w(T32_CPCCR_DEFAULT, CPM_CPCCR);
	while ((cpm_r(CPM_CPCSR) & 0xf0000007) != 0xf0000000)
		;

	pll_set(CPM_CPAPCR, T32_CPAPCR);	/* APLL 900 MHz */
	pll_set(CPM_CPMPCR, T32_CPMPCR);	/* MPLL 1200 MHz */
	pll_set(CPM_CPVPCR, T32_CPVPCR);	/* VPLL 1188 MHz (PRJ007) */

	/* cpccr_sets: program dividers, then the source selects. */
	cpm_w(T32_CPCCR_DIV, CPM_CPCCR);
	while (cpm_r(CPM_CPCSR) & 7)
		;
	cpm_w(T32_CPCCR_SEL, CPM_CPCCR);
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
 * CONFIG_SYS_MEM_FREQ / 2); for PRJ007_lq that is the MPLL-sourced
 * DDR CK of 300 MHz (600 MT/s). CPM_DDRCDR layout (vendor CGU
 * descriptor [DDR] = {.., sel_bit 30, ce 29, busy 28, stop 27}):
 * src[31:30] (1=APLL, 2=MPLL), ce[29], busy[28], stop[27],
 * divider[7:0]. cdr = MPLL / CK - 1.
 */
#define T32_EXTAL_HZ	24000000U
#define T32_DDR_CK_HZ	300000000U	/* CONFIG_SYS_MEM_FREQ 600M / 2 */

/* CPxPCR (CPAPCR/CPMPCR) -> Hz. Shared by the DDR and SFC0 clocks. */
u32 t32_pll_rate(unsigned int cpxpcr_off)
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

	return (u32)((u64)T32_EXTAL_HZ * m / n / od1 / od0);
}

void ddr_clk_init(void)
{
	u32 mpll = t32_pll_rate(CPM_CPMPCR);
	u32 cdr = ((mpll + T32_DDR_CK_HZ - 1) / T32_DDR_CK_HZ - 1) & 0xff;
	u32 v;

	v = cpm_r(CPM_DDRCDR);
	v &= ~((3u << 30) | (1u << 28) | (1u << 27) | 0xff);
	v |= (2u << 30) | (1u << 29) | cdr;	/* src=MPLL, ce, divider */
	cpm_w(v, CPM_DDRCDR);
	while (cpm_r(CPM_DDRCDR) & (1u << 28))	/* wait change-busy clear */
		;
}
