// SPDX-License-Identifier: GPL-2.0+
/*
 * T40 SPL helper: bring up the SFC0 CGU clock and program the SFC
 * controller so the standard DM SPI driver (drivers/spi/jz_sfc.c) can
 * read NOR after board_init_r(). The bootrom configured the SFC for
 * its own load, but our pll_init() / CGU re-source may leave SFCCDR
 * in an indeterminate state, so re-program it from a known PLL.
 *
 * Faithful transliteration of the vendor sfc_init() (no hand-rolling,
 * the register write order and bit fields are reproduced exactly).
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t40.h>
#include <mach/t31-sfc.h>

/* SSI/SFC clock target, from vendor sfc_init(): clk_set_rate(SSI, 70M) */
#define T40_SFC_RATE		70000000U
/*
 * MPLL rate for this profile (1000 MHz at the T40NN setpoint; same MPLL
 * the DDR clock uses, see pll.c T40_MPLL_MHZ). Vendor cgu_clk_sel[SSI]
 * selects MPLL as the SSI source (non-CONFIG_BURNER path).
 */
#define T40_MPLL_RATE		1000000000U

/*
 * SFC CGU entry: T40 CPM_SFCCDR is at offset 0x60. ce/busy/stop are
 * at bits 29/28/27 (XBurst2 CGU convention shared with T41/A1).
 */
#define SFC_CGU_CE		29
#define SFC_CGU_BUSY		28
#define SFC_CGU_STOP		27

static u32 cpm_readl(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void cpm_writel(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

static void jz_sfc_writel(unsigned int value, unsigned int offset)
{
	writel(value, (void __iomem *)(SFC_BASE + offset));
}

static void sfc_init(void)
{
	unsigned int tmp;

	/* Ungate SFC0 clock (CLKGR0 bit 24) */
	tmp = cpm_readl(CPM_CLKGR0);
	tmp &= ~CPM_CLKGR0_SFC;
	cpm_writel(tmp, CPM_CLKGR0);

	/*
	 * Re-program CPM_SFCCDR. Vendor cgu_clk_sel[SFC] = sel=0:APLL,
	 * sel=1:MPLL, sel=2:VPLL; MPLL at bits 31:30 = 01. div=80 with
	 * MPLL=1000 gives SFC ~12 MHz. Poll BUSY for completion.
	 */
	{
		u32 reg = cpm_readl(CPM_SFCCDR);
		reg &= ~((3u << 30) | (3 << SFC_CGU_STOP) | 0xff);
		reg |= (1u << 30) | (1 << SFC_CGU_CE) | 80;
		cpm_writel(reg, CPM_SFCCDR);
		while (cpm_readl(CPM_SFCCDR) & (1 << SFC_CGU_BUSY))
			;
	}

	tmp = THRESHOLD << THRESHOLD_OFFSET;
	jz_sfc_writel(tmp, SFC_GLB);

	tmp = CEDL | HOLDDL | WPDL;
	jz_sfc_writel(tmp, SFC_DEV_CONF);

	/* low power consumption */
	jz_sfc_writel(0, SFC_CGE);
}

/*
 * Bring up the SFC0 clock + controller. Used by the SPL board_init_f()
 * so the DM SPI driver (probed by the SPL framework in board_init_r())
 * and any later U-Boot proper SFC access inherits a clocked controller.
 */
void t40_spl_sfc_clk_init(void)
{
	sfc_init();
}
