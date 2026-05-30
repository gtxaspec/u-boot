// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 PLL and clock setup (SPL)
 *
 * A1 (XBurst2) has four PLLs: APLL (CPU), MPLL (DDR/peripheral),
 * VPLL and EPLL. The CPCCR register selects clock sources and the
 * core/L2/AHB0/AHB2/APB dividers; AHB1CDR is a separate register.
 *
 * A1N: APLL 1104 / MPLL 1608 (DDR 804) / VPLL 1200 / EPLL 1500
 * CPU from APLL, DDR from MPLL.
 *
 * PLL encoding: (M<<20)|(N<<14)|(OD1<<11)|(OD0<<8)
 * Pllout = 24MHz * M / (N * OD0 * OD1)
 * APLL uses N=1, OD1=2 (Pllout = 12MHz * M); MPLL 1400 uses N=3.
 */

#include <hang.h>
#include <asm/io.h>
#include <mach/a1.h>

/*
 * Per-SKU APLL/MPLL setpoints live in the DDR variant struct
 * (drivers/ram/ingenic/ddr_innophy_types.c) and are selected at runtime
 * from the DT ingenic,variant property - the same string the RAM driver
 * matches. board_init_f() calls fdtdec_setup() before pll_init() so the
 * FDT blob is available here, before driver model is up.
 *
 * VPLL (1200 MHz) and EPLL (1500 MHz) are SoC-fixed on every A1 SKU, so
 * they stay as constants rather than in the per-SKU variant table.
 */
int ingenic_ddr_pll_setpoints(const char *compatible, u32 *apll_mnod,
			      u32 *mpll_mnod, u32 *vpll_mnod);

#define A1_VPLL_MNOD	((100 << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define A1_EPLL_MNOD	((125 << 20) | (1 << 14) | (2 << 11) | (1 << 8))

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
 * CPCCR: SEL_SRC=2(EXTAL), SEL_CPLL=1(APLL), SEL_H0CLK=2(MPLL),
 *        SEL_H2CLK=2(MPLL).
 * Dividers from vendor (MPLL >= 1400 MHz band):
 *   PDIV=10, H2DIV=5, H0DIV=5, L2DIV=2, CDIV=1
 */
#define A1_CPCCR_CFG	((2 << 30) | (1 << 28) | (2 << 26) | (2 << 24) | \
			 ((10 - 1) << 16) | \
			 ((5 - 1) << 12) | \
			 ((5 - 1) << 8) | \
			 ((2 - 1) << 4) | ((1 - 1) << 0))

/*
 * AHB1CDR: SEL_AHB1 = MPLL (bits 1:0 = 0x2), DIV (bits 7:4).
 * Vendor: AHB1 = MPLL/5 with CE bit.
 */
#define A1_AHB1CDR_CFG	((1 << 8) | ((5 - 1) << 4) | 0x2)

static void cpccr_init(void)
{
	u32 cpccr, ahb1;

	/* Program dividers (low 24 bits), enable divider write bits 22:20 */
	cpccr = (cpm_readl(CPM_CPCCR) & (0xff << 24)) |
		(A1_CPCCR_CFG & ~(0xff << 24)) | (7 << 20);
	cpm_writel(cpccr, CPM_CPCCR);

	/* AHB1CDR: program div+sel, set CE */
	ahb1 = (cpm_readl(CPM_AHB1CDR) & 0x3) |
	       (A1_AHB1CDR_CFG & ~0x3) | (1 << 8);
	cpm_writel(ahb1, CPM_AHB1CDR);

	while (cpm_readl(CPM_CPCSR) & 0xf)
		;

	/*
	 * Clock source selects (high 8 bits). Clear the divider
	 * change-enable strobe (bits 22:20) too: the dividers were latched
	 * and settled above, so this write must not re-apply them.
	 */
	cpccr = (A1_CPCCR_CFG & (0xff << 24)) |
		(cpm_readl(CPM_CPCCR) & ~((0xff << 24) | (7 << 20)));
	cpm_writel(cpccr, CPM_CPCCR);

	/* AHB1CDR source select */
	ahb1 = (A1_AHB1CDR_CFG & 0x3) |
	       (cpm_readl(CPM_AHB1CDR) & ~0x3);
	cpm_writel(ahb1, CPM_AHB1CDR);
}

void pll_init(void)
{
	u32 apll, mpll, vpll;

	if (ingenic_ddr_pll_setpoints("ingenic,a1-ddr-innophy",
				      &apll, &mpll, &vpll))
		hang();
	(void)vpll;	/* A1 VPLL is SoC-fixed (A1_VPLL_MNOD), not per-SKU */

	/* Set bus select to EXTAL before reprogramming PLLs */
	cpm_writel((cpm_readl(CPM_CPCCR) & ~(0xff << 24)) | (0x55 << 24),
		   CPM_CPCCR);

	pll_set(CPM_CPAPCR, apll);
	pll_set(CPM_CPMPCR, mpll);
	pll_set(CPM_CPVPCR, A1_VPLL_MNOD);
	pll_set(CPM_CPEPCR, A1_EPLL_MNOD);
	cpccr_init();
}

void clk_ungate_uart(unsigned int idx)
{
	u32 clkgr0 = cpm_readl(CPM_CLKGR0);

	clkgr0 &= ~(CPM_CLKGR0_UART0 << idx);
	cpm_writel(clkgr0, CPM_CLKGR0);
}
