// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst2 DDR PHY (Innophy) - T40-family init / calibration /
 * post-init fixups / per-bit skew.
 *
 * Faithful port of arch/mips/mach-xburst/t40/sdram.c (vendor T40-1.3.1)
 * for use as the T40 branch of the UCLASS_RAM driver. The T41 family
 * uses ddr_innophy_phy_t41.c instead; the two paths share the controller-
 * side ddr_innophy.c (CGU init, DDRC reset/prev/post-init, MR program
 * via ddrc_dfi_init).
 *
 * What differs from the T41 path and lives here:
 *   - PHY reset:  read 0x00, clear low byte, mdelay(2), OR 0x0d, write
 *   - Driver/ODT: hard-coded T40N values (vendor t40n_phy_driver_odt)
 *   - DLL bypass writes for byte0..3 DQ/DQS DLL, cmd/ck DLL
 *   - DDR2 baseline TX delay seed (5 to cmd/dq, 2 to dqs/dqsb)
 *   - PHY REG-02 bit 3 set ("open manual per-bit de-skew")
 *   - PHY PLL via RMW writes (preserve any upper bits the bootrom set)
 *   - DQ_WIDTH respecting DDR_DW32, second PHY_RST inside pll_init,
 *     CWL/CL/AL programming, PLL_LOCK poll on bit 3
 *   - DDR2 hardware calibration: 0xa9 enable, poll 0xcc==0xf, 0xa8
 *   - Post-init PHY fixups: MEM_CFG|0x51, FIFO depth, TX write pointer
 *   - Per-bit RX/TX delay default table (vendor ddr_innophy_set_skew
 *     T40N branch)
 *
 * HW-validated on real T40NN silicon via the USB-boot dev loop in
 * the legacy arch/mips/mach-xburst/t40/sdram.c. Code here is the
 * driver-shaped equivalent.
 */

#include <log.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/err.h>

#include "ddr_innophy.h"

/*
 * T40-family Innophy PHY register offsets. The T41 layout in
 * ddr_innophy.h has different absolute addresses for PLL_FBDIV{L,H},
 * PLL_CTRL, PLL_PDIV, PLL_LOCK, CALIB_DONE, DQ_WIDTH and INIT_COMP -
 * the IP block was reshuffled between T40 (Innophy first-gen) and T41
 * (Innophy second-gen). The names match vendor mach/t40-ddr.h so the
 * sequence here is a 1:1 rename of the T40 sdram.c values.
 */
#define DDRP_T40_PHY_RST	(DDR_PHY_OFFSET + 0x00)
#define DDRP_T40_MEM_CFG	(DDR_PHY_OFFSET + 0x04)
#define DDRP_T40_TRAINING_CTRL	(DDR_PHY_OFFSET + 0x08)
#define DDRP_T40_CL		(DDR_PHY_OFFSET + 0x14)
#define DDRP_T40_AL		(DDR_PHY_OFFSET + 0x18)
#define DDRP_T40_CWL		(DDR_PHY_OFFSET + 0x1c)
#define DDRP_T40_DQ_WIDTH	(DDR_PHY_OFFSET + 0x7c)
#define DDRP_T40_PLL_FBDIV	(DDR_PHY_OFFSET + 0x80)
#define DDRP_T40_PLL_CTRL	(DDR_PHY_OFFSET + 0x84)
#define DDRP_T40_PLL_PDIV	(DDR_PHY_OFFSET + 0x88)
/*
 * Innophy status registers. These were WRONG (shifted -0x40): the port had
 * PLL_LOCK/CALIB_DONE/INIT_COMP at 0xc8/0xcc/0xd0, but the vendor
 * (arch/mips/cpu/xburst2/ddr_innophy.c, asm/ddr_innophy.h) has them at
 * 0x108/0x10c/0x110. 0xc8 is in the calibration-result region, not the PLL
 * lock word, so the lock poll was reading the wrong register and we
 * proceeded before the PHY PLL had actually locked -> DQS gating trained
 * against an unsettled clock -> intermittent cold-boot aliasing.
 */
#define DDRP_T40_PLL_LOCK	(DDR_PHY_OFFSET + 0x108)
#define DDRP_T40_CALIB_DONE	(DDR_PHY_OFFSET + 0x10c)
#define DDRP_T40_INIT_COMP	(DDR_PHY_OFFSET + 0x110)


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
static void phy_set_range(struct ingenic_ddr_priv *p, u32 reg,
			  u32 startbit, u32 bitscnt, u32 value)
{
	u32 off = DDRP_RAW_BASE + reg * 4;
	u32 mask = ((1u << bitscnt) - 1) << startbit;
	u32 v = phy_readl(p, off);

	v = (v & ~mask) | ((value << startbit) & mask);
	phy_writel(p, v, off);
}

/*
 * Vendor T40N drive-strength + ODT (t40n_phy_driver_odt).
 * Required for signal integrity; without this, DDR2 reads come back as
 * garbage even with the controller and PHY correctly programmed.
 */
static void t40n_phy_driver_odt(struct ingenic_ddr_priv *p)
{
	const u32 drvcmd = 0x4, drvclk = 0x9;
	const u32 odt = 0x5, drval = 0x3, drvah = 0x3;

	/* cmd / ck drive strength */
	phy_set_range(p, 0xb0, 0, 5, drvcmd);
	phy_set_range(p, 0xb1, 0, 5, drvcmd);
	phy_set_range(p, 0xb2, 0, 5, drvclk);
	phy_set_range(p, 0xb3, 0, 5, drvclk);
	/* DQ ODT, channel A */
	phy_set_range(p, 0xc0, 0, 5, odt);
	phy_set_range(p, 0xc1, 0, 5, odt);
	phy_set_range(p, 0xd0, 0, 5, odt);
	phy_set_range(p, 0xd1, 0, 5, odt);
	/* DQ ODT, channel B */
	phy_set_range(p, 0xe0, 0, 5, odt);
	phy_set_range(p, 0xe1, 0, 5, odt);
	phy_set_range(p, 0xf0, 0, 5, odt);
	phy_set_range(p, 0xf1, 0, 5, odt);
	/* DQ driver strength, channel A */
	phy_set_range(p, 0xc2, 0, 5, drval);
	phy_set_range(p, 0xc3, 0, 5, drval);
	phy_set_range(p, 0xd2, 0, 5, drvah);
	phy_set_range(p, 0xd3, 0, 5, drvah);
	/* DQ driver strength, channel B */
	phy_set_range(p, 0xe2, 0, 5, drval);
	phy_set_range(p, 0xe3, 0, 5, drval);
	phy_set_range(p, 0xf2, 0, 5, drvah);
	phy_set_range(p, 0xf3, 0, 5, drvah);
}

/*
 * Vendor T40XP DDR3 drive-strength + ODT (ddr_phy_cfg_driver_odt T40XP
 * branch; values from t40_ddr3_param). DDR3 drives CMD/CK/DQ much harder
 * (0xf / 0x11 / 0x11) and uses lighter ODT (0x3) than the DDR2/T40N profile
 * above (0x4 / 0x9 / 0x3, ODT 0x5). Applying the DDR2 profile to DDR3 leaves
 * the DQ lines badly underdriven (0x3 vs 0x11): signal integrity is marginal
 * and the DQS read lanes settle to a random phase on a fraction of cold
 * boots (auto-gating lands at an aliased ~0x6a instead of ~0x16), which no
 * gating sweep can recover. Driving DDR3 with its own profile aligns them.
 */
static void t40_ddr3_phy_driver_odt(struct ingenic_ddr_priv *p)
{
	const u32 drvcmd = 0xf, drvck = 0x11, odt = 0x3, drvdq = 0x11;

	/* cmd / ck drive strength */
	phy_set_range(p, 0xb0, 0, 5, drvcmd);
	phy_set_range(p, 0xb1, 0, 5, drvcmd);
	phy_set_range(p, 0xb2, 0, 5, drvck);
	phy_set_range(p, 0xb3, 0, 5, drvck);
	/* DQ ODT, channel A + B */
	phy_set_range(p, 0xc0, 0, 5, odt);
	phy_set_range(p, 0xc1, 0, 5, odt);
	phy_set_range(p, 0xd0, 0, 5, odt);
	phy_set_range(p, 0xd1, 0, 5, odt);
	phy_set_range(p, 0xe0, 0, 5, odt);
	phy_set_range(p, 0xe1, 0, 5, odt);
	phy_set_range(p, 0xf0, 0, 5, odt);
	phy_set_range(p, 0xf1, 0, 5, odt);
	/* DQ driver strength, channel A + B */
	phy_set_range(p, 0xc2, 0, 5, drvdq);
	phy_set_range(p, 0xc3, 0, 5, drvdq);
	phy_set_range(p, 0xd2, 0, 5, drvdq);
	phy_set_range(p, 0xd3, 0, 5, drvdq);
	phy_set_range(p, 0xe2, 0, 5, drvdq);
	phy_set_range(p, 0xe3, 0, 5, drvdq);
	phy_set_range(p, 0xf2, 0, 5, drvdq);
	phy_set_range(p, 0xf3, 0, 5, drvdq);
}

/*
 * DDR2 baseline TX delay seed (vendor ddr_phy_init DDR2 branch). The
 * per-bit auto-calibration the PHY runs at training time starts from
 * these regs; without an explicit seed they come up with random values
 * and HW calibration has to work harder (or fails) on quick power
 * cycles. Empirically on T40NN silicon this seed is what made USB-boot
 * quick-cycle DDR init go from intermittent to 4/4 success.
 */
static void t40_ddr2_baseline_seed(struct ingenic_ddr_priv *p)
{
	const u32 chA = 0x120, chB = 0x1a0;
	int i;

	/* cmd TX delay 0x100..0x11e */
	for (i = 0; i <= 0x1e; i++)
		phy_writel(p, 5, (0x100 + i) * 4);
	/* DQ TX delay, low half [0..8] both channels */
	for (i = 0; i <= 0x8; i++) {
		phy_writel(p, 5, (chA + i) * 4);
		phy_writel(p, 5, (chB + i) * 4);
	}
	/* DQ TX delay, high half [0xb..0x13] both channels */
	for (i = 0xb; i <= 0x13; i++) {
		phy_writel(p, 5, (chA + i) * 4);
		phy_writel(p, 5, (chB + i) * 4);
	}
	/* DQS / DQSb TX delay (per-channel, byte 0 + byte 1) = 2 */
	phy_writel(p, 2, (chA + 0x09) * 4);
	phy_writel(p, 2, (chB + 0x09) * 4);
	phy_writel(p, 2, (chA + 0x0a) * 4);
	phy_writel(p, 2, (chB + 0x0a) * 4);
	phy_writel(p, 2, (chA + 0x14) * 4);
	phy_writel(p, 2, (chB + 0x14) * 4);
	phy_writel(p, 2, (chA + 0x15) * 4);
	phy_writel(p, 2, (chB + 0x15) * 4);

	/* PHY REG-02 bit 3 = "open manual per-bit de-skew" enable */
	phy_writel(p, phy_readl(p, 0x02 * 4) | 0x8, 0x02 * 4);
}

/*
 * Vendor T40XP DDR3 write-leveling TX-delay seed + per-bit de-skew enable
 * (ddr_phy_init DDR3/T40XP branch). CMD and DQ TX delays start at 8 (vs the
 * DDR2 seed's 5); DQS TX is left at its default, and PHY REG-02 bit 3 opens
 * manual per-bit de-skew. Without this seed the DDR3 lanes intermittently
 * come up unaligned on a cold boot - the per-lane DQS lands at wildly
 * different phases (e.g. 0x16 vs 0x53 vs 0x6a) so no single per-channel
 * gating delay can recover them and DRAM stays dead even after the gating
 * sweep. Mirrors the DDR2 baseline seed but with the vendor's DDR3 values.
 */
static void t40_ddr3_baseline_seed(struct ingenic_ddr_priv *p)
{
	const u32 chA = 0x120, chB = 0x1a0;
	int i;

	/* cmd TX delay 0x100..0x11e */
	for (i = 0; i <= 0x1e; i++)
		phy_writel(p, 8, (0x100 + i) * 4);
	/* DQ TX delay, low half [0..8] both channels */
	for (i = 0; i <= 0x8; i++) {
		phy_writel(p, 8, (chA + i) * 4);
		phy_writel(p, 8, (chB + i) * 4);
	}
	/* DQ TX delay, high half [0xb..0x13] both channels */
	for (i = 0xb; i <= 0x13; i++) {
		phy_writel(p, 8, (chA + i) * 4);
		phy_writel(p, 8, (chB + i) * 4);
	}

	/* PHY REG-02 bit 3 = "open manual per-bit de-skew" enable */
	phy_writel(p, phy_readl(p, 0x02 * 4) | 0x8, 0x02 * 4);
}

/*
 * Vendor ddrp_pll_init: RMW writes (mask low byte, OR in value) to
 * preserve any upper bits the bootrom / EFUSE set. Order is fixed -
 * PHY_RST is asserted AGAIN inside this function AFTER DQ_WIDTH +
 * MEM_CFG, BEFORE CWL/CL/AL, with the PLL_LOCK poll AT THE END.
 *
 * Bus width: 16-bit by default (vendor non-DDR_DW32 branch). T40N has
 * DDR_DW32 = 1 so we set both DQ_H and DQ_L lanes.
 */
int ingenic_ddr_t40_phy_init(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;
	u32 val;

	/*
	 * PHY reset: read PHY_RST/CHANNEL_EN (offset 0), clear low byte,
	 * mdelay(2), OR 0x0d, write. The 2ms gap is intentional - it gives
	 * the PHY analog block time to settle after the prior DDRC CTRL
	 * dfi_reset_n assertion before we re-program the digital reset
	 * bits.
	 */
	val = phy_readl(p, DDRP_T40_PHY_RST);
	val &= ~0xff;
	mdelay(2);
	val |= 0x0d;
	phy_writel(p, val, DDRP_T40_PHY_RST);

	if (v->type == INGENIC_DDR_TYPE_DDR3)
		t40_ddr3_phy_driver_odt(p);
	else
		t40n_phy_driver_odt(p);

	/* DLL bypass values - vendor order: cmd/ck DLL first, then byte DLLs */
	phy_writel(p, 0xc, 0x15 * 4);	/* cmd dll */
	phy_writel(p, 0x1, 0x16 * 4);	/* ck dll */
	phy_writel(p, 0xc, 0x54 * 4);	/* byte0 dq dll */
	phy_writel(p, 0xc, 0x64 * 4);	/* byte1 dq dll */
	phy_writel(p, 0xc, 0x84 * 4);	/* byte2 dq dll */
	phy_writel(p, 0xc, 0x94 * 4);	/* byte3 dq dll */
	phy_writel(p, 0x1, 0x55 * 4);	/* byte0 dqs dll */
	phy_writel(p, 0x1, 0x65 * 4);	/* byte1 dqs dll */
	phy_writel(p, 0x1, 0x85 * 4);	/* byte2 dqs dll */
	phy_writel(p, 0x1, 0x95 * 4);	/* byte3 dqs dll */

	if (v->type == INGENIC_DDR_TYPE_DDR2)
		t40_ddr2_baseline_seed(p);
	else if (v->type == INGENIC_DDR_TYPE_DDR3)
		t40_ddr3_baseline_seed(p);

	/*
	 * PLL programming (RMW low byte) - vendor uses RMW writes to
	 * preserve any upper bits the bootrom / EFUSE set. The vendor
	 * INNO_PLL_FBDIV / INNO_PLL_CTRL / INNO_PLL_PDIV are single regs
	 * at +0x80 / +0x84 / +0x88 (T41 has them at different offsets).
	 */
	val = phy_readl(p, DDRP_T40_PLL_FBDIV); val &= ~0xff; val |= 0x10;
	phy_writel(p, val, DDRP_T40_PLL_FBDIV);

	val = phy_readl(p, DDRP_T40_PLL_CTRL); val &= ~0xff; val |= 0x1a;
	phy_writel(p, val, DDRP_T40_PLL_CTRL);

	val = phy_readl(p, DDRP_T40_PLL_PDIV); val &= ~0xff; val |= 0x4;
	phy_writel(p, val, DDRP_T40_PLL_PDIV);

	val = phy_readl(p, DDRP_T40_PLL_CTRL); val &= ~0xff; val |= 0x18;
	phy_writel(p, val, DDRP_T40_PLL_CTRL);

	val = phy_readl(p, DDRP_T40_DQ_WIDTH); val &= ~0xf;
	/* Bus-width gating: vendor T40 selects between 0xf (DW32) and
	 * 0x3 (DW16). bus_width=32 enables both lanes. */
	if (v->bus_width == 32)
		val |= 0xf;
	else
		val |= 0x3;
	phy_writel(p, val, DDRP_T40_DQ_WIDTH);

	val = phy_readl(p, DDRP_T40_MEM_CFG); val &= ~0xff; val |= v->ddrp_memcfg;
	phy_writel(p, val, DDRP_T40_MEM_CFG);

	/* 2nd PHY_RST (inside ddrp_pll_init), AFTER DQ_WIDTH + MEM_CFG. */
	val = phy_readl(p, DDRP_T40_PHY_RST); val &= ~0xff; val |= 0x0d;
	phy_writel(p, val, DDRP_T40_PHY_RST);

	val = phy_readl(p, DDRP_T40_CWL); val &= ~0xf; val |= (v->ddrp_cwl & 0xf);
	phy_writel(p, val, DDRP_T40_CWL);

	val = phy_readl(p, DDRP_T40_CL); val &= ~0xf; val |= (v->ddrp_cl & 0xf);
	phy_writel(p, val, DDRP_T40_CL);

	val = phy_readl(p, DDRP_T40_AL); val &= ~0xf; val = 0x0;	/* vendor writes AL = 0 */
	phy_writel(p, val, DDRP_T40_AL);

	/* Wait for the PHY PLL to lock - bit 3 of PLL_LOCK (@ +0x108). The
	 * vendor bounds this with a ~1M-iteration timeout and falls through
	 * rather than spinning forever. (vendor ddrp_pll_init ends here.) */
	{
		unsigned int timeout = 1000000;

		while (!(phy_readl(p, DDRP_T40_PLL_LOCK) & (1u << 3)) && --timeout)
			;
	}

	return 0;
}

/*
 * DDR2 hardware DQS calibration (vendor ddrp_hardware_calibration DDR2
 * branch): write 0xa9 (training enable + DDR2 mode), poll INNO_CALIB
 * +0xcc bits[3:0] == 0xf, write 0xa8 (clear enable).
 *
 * DDR3 branch (also lives here for completeness) sets bit 0 of
 * existing TRAINING_CTRL instead.
 */
/*
 * T40 HW DQS calibration. Vendor sdram.c writes 0xa9 (DDR2) or 0x1
 * (DDR3) to TRAINING_CTRL, polls INNO_CALIB_DONE for bits[3:0]=0xf and
 * always falls through after the timeout (vendor's `timeout--` post-
 * decrement exits without checking the result). On the T40NN lab
 * silicon the poll reliably ends with CALIB_DONE=0x0d (byte-lane 1 of
 * channel A never completes); the legacy port worked because vendor
 * accepted partial calibration and continued. Mirror that behaviour:
 * never fail the probe over calibration not hitting 0xf - subsequent
 * post-init + per-bit skew still bring DRAM up.
 */
int ingenic_ddr_t40_phy_hw_calibration(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;
	unsigned int timeout = 1000000;

	if (v->type == INGENIC_DDR_TYPE_DDR3) {
		phy_writel(p, phy_readl(p, DDRP_T40_TRAINING_CTRL) | 0x1,
			   DDRP_T40_TRAINING_CTRL);
		while (((phy_readl(p, DDRP_T40_CALIB_DONE) & 0xf) != 0xf) &&
		       --timeout)
			;
		phy_writel(p, phy_readl(p, DDRP_T40_TRAINING_CTRL) & ~0x1,
			   DDRP_T40_TRAINING_CTRL);
		return 0;
	}

	/* DDR2 */
	phy_writel(p, 0xa9, DDRP_T40_TRAINING_CTRL);
	while (((phy_readl(p, DDRP_T40_CALIB_DONE) & 0xf) != 0xf) && --timeout)
		;
	phy_writel(p, 0xa8, DDRP_T40_TRAINING_CTRL);
	return 0;
}

/*
 * Vendor T40 sdram_init() post-init PHY fixups (DDR-type specific).
 * Without these the DQ FIFO and write-pointer alignment is off and
 * reads return garbage.
 */
void ingenic_ddr_t40_post_phy_fixups(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;

	if (v->type == INGENIC_DDR_TYPE_DDR3)
		phy_set_range(p, 0x1, 6, 1, 1);			/* FIFO bit */
	else
		phy_writel(p, 0x51, DDRP_T40_MEM_CFG);		/* MEM_CFG | bit6 FIFO */
	phy_set_range(p, 0xa, 1, 3, 3);				/* FIFO depth */
	phy_set_range(p, 0x8, 0, 2, 3);				/* TX wptr adj */
}

/*
 * Vendor T40N per-bit RX/TX delay defaults the PHY uses to time DQ/DQS
 * at 500 MHz. The register layout uses chA = 0x120 and chB = 0x1a0
 * (different from the T41 set_vref_skew which uses 0x1c0 + dq_off_rx[]
 * indexing). For DDR3, the equivalent T41-style table comes through
 * ingenic_ddr_t41_phy_set_vref_skew on the T41 family.
 */
void ingenic_ddr_t40_phy_set_skew(struct ingenic_ddr_priv *p)
{
	static const u32 dqx_rx[] = {
		0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
		0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,
	};
	static const u32 dqx_tx[] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
	};
	const u32 chA = 0x120, chB = 0x1a0;
	const u32 vref = 0x7f, dqs_rx = 0x18, dqx_rx_v = 0x11;
	const u32 dqs_tx = 0x07, dqx_tx_v = 0x0e;
	int i;

	if (p->cfg->type == INGENIC_DDR_TYPE_DDR3)
		return;

	/* vref - channels A and B */
	phy_writel(p, vref, 0xd7 * 4);
	phy_writel(p, vref, 0xd8 * 4);
	phy_writel(p, vref, 0xf7 * 4);
	phy_writel(p, vref, 0xf8 * 4);

	/* RX: DQS and DQ per channel */
	phy_writel(p, dqs_rx, (chA + 0x29) * 4);
	phy_writel(p, dqs_rx, (chA + 0x34) * 4);
	phy_writel(p, dqs_rx, (chB + 0x29) * 4);
	phy_writel(p, dqs_rx, (chB + 0x34) * 4);
	for (i = 0; i < 16; i++) {
		phy_writel(p, dqx_rx_v, (chA + dqx_rx[i]) * 4);
		phy_writel(p, dqx_rx_v, (chB + dqx_rx[i]) * 4);
	}

	/* TX: cmd (0x100..0x11e), then DM/DQS/DQSb per channel, then DQ */
	for (i = 0; i <= 0x1e; i++)
		phy_writel(p, dqs_tx, (0x100 + i) * 4);
	phy_writel(p, dqs_tx, (chA + 0x00) * 4);	/* dm0 */
	phy_writel(p, dqs_tx, (chA + 0x09) * 4);	/* dqs0 */
	phy_writel(p, dqs_tx, (chA + 0x0a) * 4);	/* dqsb0 */
	phy_writel(p, dqs_tx, (chA + 0x0b) * 4);	/* dm1 */
	phy_writel(p, dqs_tx, (chA + 0x14) * 4);	/* dqs1 */
	phy_writel(p, dqs_tx, (chA + 0x15) * 4);	/* dqsb1 */
	phy_writel(p, dqs_tx, (chB + 0x00) * 4);
	phy_writel(p, dqs_tx, (chB + 0x09) * 4);
	phy_writel(p, dqs_tx, (chB + 0x0a) * 4);
	phy_writel(p, dqs_tx, (chB + 0x0b) * 4);
	phy_writel(p, dqs_tx, (chB + 0x14) * 4);
	phy_writel(p, dqs_tx, (chB + 0x15) * 4);
	for (i = 0; i < 16; i++) {
		phy_writel(p, dqx_tx_v, (chA + dqx_tx[i]) * 4);
		phy_writel(p, dqx_tx_v, (chB + dqx_tx[i]) * 4);
	}
}
