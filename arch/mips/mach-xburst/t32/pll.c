// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 PLL and clock setup (SPL)
 *
 * Forward-port of the vendor U-Boot 2022.10 PRJ pllsetting.c for the
 * T32 (PRJ007). T32 uses the M/N/OD0/OD1 CPAPCR/CPMPCR/CPVPCR form
 * (like T31/T23/T20). The per-SKU APLL/MPLL words and the two-stage
 * CPCCR programming words (dividers, then source selects) live in the
 * DDR variant struct (drivers/ram/ingenic/ddr_t32_types.c) and are
 * selected at runtime by matching the &ddr node's per-SKU compatible
 * (ingenic,t32<sku>-ddr-innophy) - the same of_match table the RAM
 * driver uses. soc.c calls fdtdec_setup() before pll_init() so the
 * FDT blob is available here, before driver model is up.
 *
 * UNLIKE T33/PRJ008, T32/PRJ007 DOES program VPLL (vendor pll_sets()
 * skips it only for PRJ008); VPLL is SoC-fixed at 1188 MHz on every
 * SKU so it stays a constant here rather than in the variant table.
 * CPxPCR = (EN<<0)|(M<<20)|(N<<14)|(OD1<<11)|(OD0<<8)|(1<<7)|(1<<6).
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <hang.h>
#include <asm/io.h>
#include <mach/t32.h>

#define T32_CPVPCR	0x0c609101u	/* VPLL 1188 MHz (vendor SPL verified) */
#define T32_CPCCR_DEFAULT	0x55700000u

/*
 * SPL helper from the T32 DDR driver: find the DDR node in the FDT (by
 * its per-SKU compatible) and return that SKU's CPAPCR/CPMPCR words and
 * the two-stage CPCCR programming words. Runs before driver model is up.
 */
int ingenic_t32_ddr_pll_setpoints(u32 *cpapcr, u32 *cpmpcr,
				  u32 *cpccr_div, u32 *cpccr_sel);

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
	u32 cpapcr, cpmpcr, cpccr_div, cpccr_sel;

	if (ingenic_t32_ddr_pll_setpoints(&cpapcr, &cpmpcr,
					  &cpccr_div, &cpccr_sel))
		hang();

	/* cpccr_default: known state, wait CPCSR stable. */
	cpm_w(T32_CPCCR_DEFAULT, CPM_CPCCR);
	while ((cpm_r(CPM_CPCSR) & 0xf0000007) != 0xf0000000)
		;

	pll_set(CPM_CPAPCR, cpapcr);
	pll_set(CPM_CPMPCR, cpmpcr);
	pll_set(CPM_CPVPCR, T32_CPVPCR);	/* VPLL (PRJ007 programs it) */

	/* cpccr_sets: program dividers, then the source selects. */
	cpm_w(cpccr_div, CPM_CPCCR);
	while (cpm_r(CPM_CPCSR) & 7)
		;
	cpm_w(cpccr_sel, CPM_CPCCR);
	while ((cpm_r(CPM_CPCSR) & 0xf0000000) != 0xf0000000)
		;
}

/* Ungate the console UART (CLKGR0: UART0 = bit 11, UART1 = bit 12). */
void clk_ungate_uart(unsigned int idx)
{
	cpm_w(cpm_r(CPM_CLKGR0) & ~(CPM_CLKGR0_UART0 << idx), CPM_CLKGR0);
}

/* CPxPCR (CPAPCR/CPMPCR) -> Hz. Used by the SPL SFC clock (sfc.c). */
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

	return (u32)((u64)24000000 * m / n / od1 / od0);
}
