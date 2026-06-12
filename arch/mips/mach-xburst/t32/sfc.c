// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 SPL SFC clock + controller bring-up.
 *
 * After PLL + DDR init (UCLASS_RAM driver), the T32 SPL hands U-Boot
 * loading to the standard SPL_SPI framework (NOR cold-boot, via
 * board_init_r) or returns to the mask ROM (USB-boot). Both need the
 * SFC clock derived off MPLL and the controller GLB0/DEV_CONF
 * programmed before the DM SFC driver (or U-Boot proper, on USB-boot)
 * touches the flash.
 *
 * T32 (SFC2) clocks the SFC off the dedicated SFC0 divider
 * (CPM_SFC0CDR), not the shared SSI divider T31 uses. A hand-rolled
 * SFC2 NOR read + LZMA loader used to live here; it is gone now that
 * the NOR path uses the standard SPL_SPI flow with the DM SFC driver
 * (drivers/spi/ingenic_sfc.c), the same reliable read U-Boot proper
 * uses.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t32.h>
#include <mach/t32-sfc.h>

u32 t32_pll_rate(unsigned int cpxpcr_off);

static u32 cpm_r(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void cpm_w(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

static u32 sfc_r(unsigned int off)
{
	return readl((void __iomem *)(SFC_BASE + off));
}

static void sfc_w(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(SFC_BASE + off));
}

/*
 * SFC0 branch of the vendor _clk_set_rate(): preserve the source
 * select [31:30] (left valid by the bootrom) and the [9:8] pre-
 * divide; reprogram only the divider + change-enable, then wait for
 * the busy bit to clear. src 0=APLL 1=MPLL 2=VPLL.
 */
static void sfc0_clk_set_rate(unsigned int rate)
{
	u32 regval = cpm_r(CPM_SFC0CDR);
	u32 src = regval >> 30;
	u32 pll = t32_pll_rate(src == 0 ? CPM_CPAPCR : CPM_CPMPCR);
	u32 ori_sel = 1u << ((regval >> 8) & 0x3);
	u32 cdr = (((pll + rate - 1) / rate) / ori_sel - 1) & 0xff;

	regval &= ~((3 << SFC0_CGU_STOP) | 0xff);
	regval |= (1 << SFC0_CGU_CE) | cdr;
	cpm_w(regval, CPM_SFC0CDR);
	while (cpm_r(CPM_SFC0CDR) & (1 << SFC0_CGU_BUSY))
		;
}

/*
 * Bring up the SFC clock + controller for the DM SPI driver. Called
 * from board_init_f before board_init_r runs the SPL_SPI load (NOR
 * cold-boot) or before returning to the mask ROM (USB-boot). The DM
 * SFC driver expects the clock derived and the GLB0 threshold /
 * DEV_CONF line-enable delays already programmed.
 */
void t32_spl_sfc_clk_init(void)
{
	u32 reg;

	sfc0_clk_set_rate(SFC0_INIT_RATE);

	reg = sfc_r(SFC_GLB0);
	reg &= ~(GLB_TRAN_DIR | GLB_OP_MODE | GLB_THRESHOLD_MSK);
	reg |= GLB_WP_EN | (SFC_THRESHOLD << GLB_THRESHOLD_OFFSET);
	sfc_w(reg, SFC_GLB0);

	reg = sfc_r(SFC_DEV_CONF);
	reg |= DEV_CONF_CEDL | DEV_CONF_HOLDDL | DEV_CONF_WPDL;
	sfc_w(reg, SFC_DEV_CONF);

	/* low power consumption */
	sfc_w(0, SFC_CGE);

	/* bootrom may leave the engine running - stop & flush the FIFO */
	sfc_w(TRIG_STOP, SFC_TRIG);
	sfc_w(TRIG_FLUSH, SFC_TRIG);
	sfc_w(0, SFC_TRAN_LEN);
}
