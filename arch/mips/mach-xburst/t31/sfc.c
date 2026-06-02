// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 SPL SFC clock + controller bring-up.
 *
 * After PLL + (hand-rolled) DDR init, the T31 SPL hands U-Boot loading
 * to the standard SPL_SPI framework (NOR cold-boot, via board_init_r)
 * or returns to the mask ROM (USB-boot). Both need the SFC clock
 * derived off MPLL and the controller GLB/DEV_CONF programmed before
 * the DM SPI driver (or U-Boot proper, on USB-boot) touches the flash.
 *
 * T31 (XBurst1) clocks the SFC off the SSI clock divider (CPM_SSICDR),
 * not the XBurst2 SFC0CDR. A hand-rolled NOR read + LZMA loader used to
 * live here; it is gone now that the NOR path uses the standard
 * SPL_SPI flow with the DM SFC driver (drivers/spi/ingenic_sfc.c), the
 * same reliable read U-Boot proper uses.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t31.h>
#include <mach/t31-sfc.h>

/*
 * SSI cgu entry from the vendor cgu_clk_sel[] table (clk.c):
 *   [SSI] = {1, CPM_SSICDR, 30, MPLL, {APLL, MPLL, VPLL}, 28, 27, 26}
 * source-mux at bits 31:30 (MPLL = 1), ce = bit 28, busy = bit 27,
 * stop = bit 26. These bits differ from the DDR entry; do not guess.
 */
#define CPM_SSICDR	0x74
#define SSI_CGU_CE	28
#define SSI_CGU_BUSY	27
#define SSI_CGU_STOP	26

/*
 * MPLL = 1200 MHz (same MPLL the DDR clock uses). The SFC read clock is
 * kept conservative at 40 MHz: the SPL-side load is small, the DM SFC
 * driver reads at this rate, and U-Boot proper re-rates for its own
 * faster reads. SSI source select = MPLL (index 1 of {APLL,MPLL,VPLL}).
 */
#define T31_MPLL_RATE	1200000000U
#define T31_SSI_RATE	40000000U

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

/*
 * SSI branch of the vendor clk_set_rate() (clk.c). SSI is not MSC0/MSC1
 * and not DDR, so cdr = (pll_rate/rate - 1) & 0xff, with pll_rate
 * rounded to a multiple of rate exactly as the vendor does.
 */
static void ssi_clk_set_rate(void)
{
	unsigned int pll_rate = T31_MPLL_RATE;
	unsigned int rate = T31_SSI_RATE;
	unsigned int cdr;
	u32 regval;

	regval = cpm_readl(CPM_SSICDR);

	if (pll_rate % rate >= rate / 2)
		pll_rate += rate - (pll_rate % rate);
	else
		pll_rate -= (pll_rate % rate);

	cdr = (pll_rate / rate - 1) & 0xff;

	regval &= ~((3 << SSI_CGU_STOP) | 0xff);
	regval &= ~(0x3 << 30);
	regval |= (0x1 << 30);			/* source = MPLL */
	regval |= ((1 << SSI_CGU_CE) | cdr);
	cpm_writel(regval, CPM_SSICDR);
	while (cpm_readl(CPM_SSICDR) & (1 << SSI_CGU_BUSY))
		;
}

/*
 * Bring up the SFC clock + controller for the DM SPI driver. Called
 * from board_init_f before board_init_r runs the SPL_SPI load (NOR
 * cold-boot) or before returning to the mask ROM (USB-boot). The DM SFC
 * driver expects the clock derived and the GLB threshold / DEV_CONF
 * line-enable delays already programmed.
 */
void t31_spl_sfc_clk_init(void)
{
	ssi_clk_set_rate();

	jz_sfc_writel(THRESHOLD << THRESHOLD_OFFSET, SFC_GLB);
	jz_sfc_writel(CEDL | HOLDDL | WPDL, SFC_DEV_CONF);

	/* low power consumption */
	jz_sfc_writel(0, SFC_CGE);
}
