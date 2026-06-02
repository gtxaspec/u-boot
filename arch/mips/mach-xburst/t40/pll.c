// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T40 PLL and clock setup (SPL)
 *
 * T40 (XBurst2) has four PLLs: APLL (CPU), MPLL (DDR/peripheral),
 * EPLL, and VPLL. CPM_CPCCR selects clock sources and the core
 * dividers.
 *
 * Default T40: APLL 1404 MHz / MPLL 1000 MHz (DDR 500). PLL encoding
 * matches A1: (M<<20)|(N<<14)|(OD1<<11)|(OD0<<8).
 *
 * TODO: this is a faithful transliteration of the vendor SPL clk path
 * boiled down to the bring-up minimum. Real-silicon validation will
 * surface deltas (the vendor SPL has a switch-table indexed by EFUSE
 * subsoctype to pick the variant clock plan); for now T40-DDR2-500 is
 * the only target.
 */

#include <hang.h>
#include <asm/io.h>
#include <mach/t40.h>

/*
 * The per-SKU APLL/MPLL setpoints live in the DDR variant struct
 * (drivers/ram/ingenic/ddr_innophy_types.c) and are selected at runtime
 * from the DT ingenic,variant property - the same string the RAM driver
 * uses. board_init_f() calls fdtdec_setup() before pll_init() so the
 * FDT blob is available here, before driver model is up. (The T40 PLL
 * plan uses only APLL + MPLL; VPLL is unused.)
 */
int ingenic_ddr_pll_setpoints(u32 *apll_mnod, u32 *mpll_mnod,
			      u32 *vpll_mnod);

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
	u32 cur;

	/*
	 * If the bootrom left this PLL enabled at a different setpoint,
	 * disable it before writing the new M/N/OD - relocking a live PLL
	 * to a new rate can be unclean. Skip the disable when the PLL is
	 * off or already on target (the compare ignores the low byte, where
	 * PLLEN/PLLON live). Safe to gate the PLL here: pll_init() has
	 * already parked the CPU and bus clocks on EXTAL.
	 */
	cur = cpm_readl(reg);
	if ((cur & PLL_PLLEN) && ((cur & ~0xff) != (mnod & ~0xff))) {
		volatile int d;
		cpm_writel(cur & ~PLL_PLLEN, reg);
		for (d = 0; d < 1000; d++);	/* ~1us busy-wait (no timer yet) */
	}
	cpm_writel(mnod | PLL_PLLEN, reg);
	while (!(cpm_readl(reg) & PLL_PLLON))
		;
}

/*
 * CPCCR vendor T40N production values per include/configs/isvp_t40.h
 * DDR_500M block: PDIV=12, H2DIV=6, H0DIV=6, L2DIV=2, CDIV=1. SEL
 * bits chosen for APLL=CPU clock + MPLL=H0/H2/peripherals. Encoded
 * value = 0x9a0b5510 (matches vendor T40 INGE descriptor SEL value).
 */
#define T40_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
			 ((12 - 1) << 16) | ((6 - 1) << 12) | \
			 ((6 - 1) << 8) | ((2 - 1) << 4) | ((1 - 1) << 0))

static void cpccr_init(void)
{
	u32 cpccr;

	cpccr = (cpm_readl(CPM_CPCCR) & (0xff << 24)) |
		(T40_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);

	while (cpm_readl(CPM_CPCSR) & 0xf)
		;

	/*
	 * Clock source selects (high 8 bits). Clear the divider
	 * change-enable strobe (bits 22:20) too: the dividers were latched
	 * and settled above, so this write must not re-apply them.
	 */
	cpccr = (T40_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~((0xff << 24) | (7 << 20)));
	cpm_writel(cpccr, CPM_CPCCR);
}

void pll_init(void)
{
	u32 apll, mpll, vpll;

	if (ingenic_ddr_pll_setpoints(&apll, &mpll, &vpll))
		hang();
	(void)vpll;	/* T40 PLL plan: APLL + MPLL only */

	cpm_writel((cpm_readl(CPM_CPCCR) & ~(0xff << 24)) | (0x55 << 24),
		   CPM_CPCCR);

	pll_set(CPM_CPAPCR, apll);
	pll_set(CPM_CPMPCR, mpll);
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
