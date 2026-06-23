// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 PLL and clock setup (SPL)
 *
 * Forward-port of the vendor U-Boot 2022.10 PRJ pllsetting.c for the
 * T32 (PRJ007). T32 uses the M/N/OD0/OD1 CPAPCR/CPMPCR/CPVPCR form
 * (like T31/T23/T20). pll_init_params() takes the per-SKU APLL/MPLL words
 * and the two-stage CPCCR programming words (dividers, then source
 * selects) as arguments; the ddr_t32 UCLASS_RAM probe reads them from its
 * platdata (the &ddr node's ingenic,sdram-params) and calls this before
 * the DDR init - so the cache-as-RAM TPL never needs the FDT. It also runs
 * the vendor pre-PLL pokes (OST gate / watchdog / MESTSEL) that the old
 * single-stage soc.c did before pll_init().
 *
 * UNLIKE T33/PRJ008, T32/PRJ007 DOES program VPLL (vendor pll_sets()
 * skips it only for PRJ008); VPLL is SoC-fixed at 1188 MHz on every
 * SKU so it stays a constant here rather than in the variant table.
 * CPxPCR = (EN<<0)|(M<<20)|(N<<14)|(OD1<<11)|(OD0<<8)|(1<<7)|(1<<6).
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t32.h>

#define T32_CPVPCR	0x0c609101u	/* VPLL 1188 MHz (vendor SPL verified) */
#define T32_CPCCR_DEFAULT	0x55700000u

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

/*
 * Program the PLLs from the SKU setpoints (the ddr_t32 UCLASS_RAM probe
 * passes these from its platdata). DDR is not up yet, so this is the
 * earliest the TPL runs SoC code: first the vendor pre-PLL pokes - clear
 * the OST gate bit within CLKGR0, disable the watchdog, set the low
 * MESTSEL bits - then the PLLs and CPCCR.
 */
void pll_init_params(u32 cpapcr, u32 cpmpcr, u32 cpccr_div, u32 cpccr_sel)
{
	cpm_w(cpm_r(CPM_CLKGR0) & ~CPM_CLKGR1_OST, CPM_CLKGR0);
	writel(0, (void __iomem *)(WDT_BASE + WDT_TCER));
	cpm_w(cpm_r(CPM_MESTSEL) | 0x7, CPM_MESTSEL);

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

	/*
	 * Guard the divisors (defensive - the words are fixed nonzero
	 * constants, but never divide by a stray 0 field).
	 */
	if (!n)
		n = 1;
	if (!od1)
		od1 = 1;
	if (!od0)
		od0 = 1;

	return (u32)((u64)24000000 * m / n / od1 / od0);
}
