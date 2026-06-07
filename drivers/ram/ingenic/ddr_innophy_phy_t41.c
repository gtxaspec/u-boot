// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst2 DDR PHY (Innophy) - PLL/DLL bring-up, hardware DQS
 * calibration, drive/ODT programming, per-bit VREF + skew override.
 *
 * Ported from vendor U-Boot T41-1.2.6 arch/mips/cpu/xburst2/
 * ddr_innophy.c (the PHY-side helpers split out for readability).
 * Init sequence and PLL postdiv selection table preserved as-is.
 */

#include <log.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/err.h>

#include "ddr_innophy.h"

/* PHY raw register at byte offset off-from-DDRC_BASE. */
static inline u32 phy_readl(struct ingenic_ddr_priv *p, u32 off)
{
	return readl(p->base + off);
}

static inline void phy_writel(struct ingenic_ddr_priv *p, u32 val, u32 off)
{
	writel(val, p->base + off);
}

/* Bit-range RMW on a 32-bit PHY word addressed as (PHY_BASE + reg*4). */
static void phy_reg_set_range(struct ingenic_ddr_priv *p, u32 reg,
			       u32 startbit, u32 bitscnt, u32 value)
{
	u32 off = DDRP_RAW_BASE + reg * 4;
	u32 mask = (~0u << startbit) &
		   (~0u >> (32 - startbit - bitscnt));
	u32 v = phy_readl(p, off);

	v = (v & ~mask) | ((value << startbit) & mask);
	phy_writel(p, v, off);
}

/* ------------------------------------------------------------------
 * PHY PLL bring-up (vendor ddr_phy_init).
 *
 * Postdiv selection:
 *   0x20: 625-1066 MHz
 *   0x40: 280-625
 *   0x60: 140-280
 *   0xc0: 70-140
 *   0xe0: 35-70
 *
 * Vendor retries with the alternate PLL select if lock fails - we
 * inherit that, capped at one retry to avoid infinite loops.
 * ------------------------------------------------------------------ */
int ingenic_ddr_t41_phy_init(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;
	unsigned int rate = v->ddr_hz;
	u32 val;
	int pll_sel = 0;
	int attempts;

retry:
	if (pll_sel > 1) {
		log_err("ingenic-ddr: PHY PLL failed to lock\n");
		return -EIO;
	}

	/* pllfbdiv low byte */
	val = phy_readl(p, DDRP_PLL_FBDIVL);
	val = (val & ~0xff) | 0x01;
	phy_writel(p, val, DDRP_PLL_FBDIVL);

	/* pllfbdiv high + postdiven */
	val = phy_readl(p, DDRP_PLL_FBDIVH);
	val = (val & ~0xff) | 0x80;
	phy_writel(p, val, DDRP_PLL_FBDIVH);

	/* pllpostdiv */
	val = phy_readl(p, DDRP_PLL_CTRL);
	val &= ~0xff;
	if (rate > 625000000)
		val |= pll_sel ? 0x48 : 0x28;
	else
		val |= pll_sel ? 0x28 : 0x48;
	phy_writel(p, val, DDRP_PLL_CTRL);
	udelay(500);

	/* pllprediv */
	val = phy_readl(p, DDRP_PLL_PDIV);
	val = (val & ~0x1f) | 0x01;
	phy_writel(p, val, DDRP_PLL_PDIV);

	val = phy_readl(p, DDRP_PLL_CTRL);
	val &= ~0xff;
	if (rate > 625000000)
		val = pll_sel ? 0x40 : 0x20;
	else
		val = pll_sel ? 0x20 : 0x40;
	phy_writel(p, val, DDRP_PLL_CTRL);
	udelay(500);

	attempts = 0;
	while (!(phy_readl(p, DDRP_PLL_LOCK) & (1u << 2))) {
		if (++attempts > 500) {
			pll_sel++;
			ddr_writel(p, 0xf << 20, DDRC_CTRL);
			mdelay(1);
			ddr_writel(p, 0x8 << 20, DDRC_CTRL);
			mdelay(1);
			goto retry;
		}
	}

	/* Memory config: type bits live in DDRP_MEMCFG, low byte from
	 * vendor ddr_params_creator output. */
	val = phy_readl(p, DDRP_MEM_CFG);
	val = (val & ~0xff) | v->ddrp_memcfg;
	phy_writel(p, val, DDRP_MEM_CFG);

	/* 16-bit bus: both DQ_H and DQ_L enabled. */
	phy_writel(p, DDRP_DQ_WIDTH_DQ_H | DDRP_DQ_WIDTH_DQ_L, DDRP_DQ_WIDTH);

	/* PHY reset/release sequence (vendor magic). */
	val = phy_readl(p, DDRP_INNO_PHY_RST);
	val = (val & ~0xff) | 0x0d;
	phy_writel(p, val, DDRP_INNO_PHY_RST);

	/* CWL / CL latencies */
	val = phy_readl(p, DDRP_CWL);
	val = (val & ~0xf) | (v->ddrp_cwl & 0xf);
	phy_writel(p, val, DDRP_CWL);

	val = phy_readl(p, DDRP_CL);
	val = (val & ~0xf) | (v->ddrp_cl & 0xf);
	phy_writel(p, val, DDRP_CL);

	/* AL (additive latency) - vendor unconditionally clears. */
	phy_writel(p, 0, DDRP_AL);

	return 0;
}

/* ------------------------------------------------------------------
 * Hardware DQS calibration (always enabled).
 * Polls DDRP_CALIB_DONE for AL+AH (bits 0 and 1) both done.
 * ------------------------------------------------------------------ */
int ingenic_ddr_t41_phy_hw_calibration(struct ingenic_ddr_priv *p)
{
	unsigned int timeout = 1000000;
	u32 val;

	phy_writel(p, 0x0, DDRP_TRAINING_CTRL);
	(void)phy_readl(p, DDRP_TRAINING_CTRL);
	phy_writel(p, 0x1, DDRP_TRAINING_CTRL);

	do {
		val = phy_readl(p, DDRP_CALIB_DONE);
	} while (((val & 0xf) != 0x3) && --timeout);

	if (!timeout) {
		log_err("ingenic-ddr: HW DQS calibration timeout, CALIB_DONE=0x%x\n",
			phy_readl(p, DDRP_CALIB_DONE));
		return -ETIMEDOUT;
	}

	phy_writel(p, 0x0, DDRP_TRAINING_CTRL);
	return 0;
}

/* ------------------------------------------------------------------
 * Drive strength / ODT programming.
 * Vendor uses raw word writes at DDR_PHY_BASE + 4*<reg> indices; we
 * replicate. The 12 writes pair pull-up and pull-down for each lane
 * group: ODT, CMD, CLK, DQ.
 * ------------------------------------------------------------------ */
void ingenic_ddr_t41_phy_set_drv_odt(struct ingenic_ddr_priv *p)
{
	const unsigned int *par = p->cfg->par;

	phy_writel(p, par[IDP_ODT_PD],    DDRP_RAW_BASE + 4 * 0x140);
	phy_writel(p, par[IDP_ODT_PU],    DDRP_RAW_BASE + 4 * 0x141);
	phy_writel(p, par[IDP_ODT_PD],    DDRP_RAW_BASE + 4 * 0x150);
	phy_writel(p, par[IDP_ODT_PU],    DDRP_RAW_BASE + 4 * 0x151);

	phy_writel(p, par[IDP_CMD_RC_PD], DDRP_RAW_BASE + 4 * 0x130);
	phy_writel(p, par[IDP_CMD_RC_PU], DDRP_RAW_BASE + 4 * 0x131);
	phy_writel(p, par[IDP_CLK_RC_PD], DDRP_RAW_BASE + 4 * 0x132);
	phy_writel(p, par[IDP_CLK_RC_PU], DDRP_RAW_BASE + 4 * 0x133);

	phy_writel(p, par[IDP_DQX_RC_PD], DDRP_RAW_BASE + 4 * 0x142);
	phy_writel(p, par[IDP_DQX_RC_PU], DDRP_RAW_BASE + 4 * 0x143);
	phy_writel(p, par[IDP_DQX_RC_PD], DDRP_RAW_BASE + 4 * 0x152);
	phy_writel(p, par[IDP_DQX_RC_PU], DDRP_RAW_BASE + 4 * 0x153);
}

/* ------------------------------------------------------------------
 * Per-bit VREF + skew override (vendor ddr_set_vref_skew).
 * Triggered only when par[IDP_SKEW_TRX] is non-zero (RX bit 0, TX bit 1).
 * Most variants leave this 0 (vendor defaults) but T41NQ uses TRX=0x3.
 * ------------------------------------------------------------------ */
void ingenic_ddr_t41_phy_set_vref_skew(struct ingenic_ddr_priv *p)
{
	static const int dq_off_rx[] = { 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c,
					 0x0e, 0x10, 0x17, 0x19, 0x1b, 0x1d,
					 0x1f, 0x21, 0x23, 0x25 };
	static const int dq_off_tx[] = { 0x03, 0x05, 0x07, 0x09, 0x0b, 0x0d,
					 0x0f, 0x11, 0x18, 0x1a, 0x1c, 0x1e,
					 0x20, 0x22, 0x24, 0x26 };
	const unsigned int *par = p->cfg->par;
	u32 trx = par[IDP_SKEW_TRX];
	u32 wl;
	int i;

	if (!trx)
		return;

	/* VREF AL / AH */
	phy_reg_set_range(p, 0x147, 0, 8, par[IDP_VREF]);
	phy_reg_set_range(p, 0x157, 0, 8, par[IDP_VREF]);

	if (trx & 0x1) {
		phy_writel(p, par[IDP_SKEW_DQS0R], DDRP_RAW_BASE + (0x1c0 + 0x00) * 4);
		phy_writel(p, par[IDP_SKEW_DQS0R], DDRP_RAW_BASE + (0x1c0 + 0x12) * 4);
		phy_writel(p, par[IDP_SKEW_DQS0R], DDRP_RAW_BASE + (0x1c0 + 0x2a) * 4);
		phy_writel(p, par[IDP_SKEW_DQS1R], DDRP_RAW_BASE + (0x1c0 + 0x15) * 4);
		phy_writel(p, par[IDP_SKEW_DQS1R], DDRP_RAW_BASE + (0x1c0 + 0x27) * 4);
		phy_writel(p, par[IDP_SKEW_DQS1R], DDRP_RAW_BASE + (0x1c0 + 0x2b) * 4);
		for (i = 0; i < 16; i++)
			phy_writel(p, par[IDP_SKEW_DQRX],
				   DDRP_RAW_BASE + (0x1c0 + dq_off_rx[i]) * 4);
	}

	if (trx & 0x2) {
		wl = phy_readl(p, DDRP_RAW_BASE + 0x2 * 4);
		wl |= 0x8;
		phy_writel(p, wl, DDRP_RAW_BASE + 0x2 * 4);

		phy_writel(p, par[IDP_SKEW_DQS0T], DDRP_RAW_BASE + (0x1c0 + 0x01) * 4);
		phy_writel(p, par[IDP_SKEW_DQS0T], DDRP_RAW_BASE + (0x1c0 + 0x13) * 4);
		phy_writel(p, par[IDP_SKEW_DQS0T], DDRP_RAW_BASE + (0x1c0 + 0x14) * 4);
		phy_writel(p, par[IDP_SKEW_DQS1T], DDRP_RAW_BASE + (0x1c0 + 0x16) * 4);
		phy_writel(p, par[IDP_SKEW_DQS1T], DDRP_RAW_BASE + (0x1c0 + 0x28) * 4);
		phy_writel(p, par[IDP_SKEW_DQS1T], DDRP_RAW_BASE + (0x1c0 + 0x29) * 4);

		for (i = 0; i < 0x1d; i++)
			phy_writel(p, par[IDP_SKEW_DQS0T],
				   DDRP_RAW_BASE + (0x340 + i) * 4);
		phy_writel(p, par[IDP_SKEW_DQS0T],
			   DDRP_RAW_BASE + (0x340 + 0x1e) * 4);
		phy_writel(p, par[IDP_SKEW_DQS0T],
			   DDRP_RAW_BASE + (0x340 + 0x1f) * 4);

		for (i = 0; i < 16; i++)
			phy_writel(p, par[IDP_SKEW_DQTX],
				   DDRP_RAW_BASE + (0x1c0 + dq_off_tx[i]) * 4);
	}
}
