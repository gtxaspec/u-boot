// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 (XBurst2) Innophy DDR3 PHY paths for the UCLASS_RAM driver.
 *
 * Transcribed register-for-register from the vendor A1-1.7.0
 * arch/mips/cpu/xburst2/ddr_innophy.c DDR3 path (the same source our
 * arch/mips/mach-xburst/a1/sdram.c was ported from, HW-validated on
 * A1N silicon). The DDRC controller side (reset/dfi/prev/post/autosr,
 * MR programming) is shared in ddr_innophy.c; this file carries the
 * A1-family CGU + Innophy PHY paths dispatched from
 * ingenic_ddr_sdram_init().
 *
 * A1 vs T40/T41 family deltas handled here:
 *   - CPM DDRCDR lives at CPM+0x3c (T40/T41 use +0x2c) and A1 leaves the
 *     clock-source mux to the bootrom, so A1 needs its own CGU path.
 *   - PHY PLL lock is PHY+0x180 bit2; DQ width at PHY+0x034; the drive/
 *     ODT/DQS/DQ/VREF tuning uses the per-index ddr3_param layout in the
 *     a1_phy sub-struct rather than the T41 efuse par[] table.
 *   - Stock A1 trains with hardware calibration only (vendor
 *     CONFIG_DDR_SOFT_TRAINING is disabled); the 32-bit B-channel soft
 *     sweep is intentionally not ported (HW calibration covers all four
 *     lanes). Only A1N (16-bit) is validated silicon.
 */

#include <asm/io.h>
#include <linux/delay.h>

#include "ddr_innophy.h"

/* ----- A1 CPM (the A1 CPM map is shifted vs T40/T41) ----- */
#define A1_CPM_BASE		0xb0000000u
#define A1_CPM_CLKGR0		0x30u
#define A1_CPM_CLKGR0_DDR	(1u << 3)
#define A1_CPM_DDRCDR		0x3cu		/* T40/T41 use 0x2c */

/* ----- A1 Innophy PHY register byte offsets (from the PHY base, i.e.
 * DDRC base + DDR_PHY_OFFSET). Mirrors vendor ddrp register indices
 * scaled by 4 (index N -> byte N*4). ----- */
#define A1P_INNO_PHY_RST	0x000		/* idx 0x00 */
#define A1P_MEM_CFG		0x004		/* idx 0x01 */
#define A1P_TRAINING_CTRL	0x008		/* idx 0x02 */
#define A1P_CL			0x014		/* idx 0x05 */
#define A1P_AL			0x018		/* idx 0x06 */
#define A1P_CWL			0x01c		/* idx 0x07 */
#define A1P_RFIFO		0x038		/* idx 0x0e */
#define A1P_DQ_WIDTH		0x034		/* idx 0x0d */
#define A1P_PLL_PD		0x14c		/* idx 0x53 */
#define A1P_PLL_LOCK		0x180		/* idx 0x60 */
#define A1P_CALIB_DONE		0x184		/* idx 0x61 */

/* PHY access is DDRC-base-relative through DDR_PHY_OFFSET (p->base is the
 * DDRC controller base; p->base + DDR_PHY_OFFSET == 0xb3011000). */
static inline void a1p_w(struct ingenic_ddr_priv *p, u32 off, u32 val)
{
	ddr_writel(p, val, DDR_PHY_OFFSET + off);
}

static inline u32 a1p_r(struct ingenic_ddr_priv *p, u32 off)
{
	return ddr_readl(p, DDR_PHY_OFFSET + off);
}

/* Indexed PHY write: vendor addresses these by register index (byte =
 * index * 4), e.g. the drive/ODT/DQS/DQ tuning registers above 0x100. */
static inline void a1p_idx(struct ingenic_ddr_priv *p, u32 idx, u32 val)
{
	ddr_writel(p, val, DDR_PHY_OFFSET + idx * 4);
}

static void a1p_rmw(struct ingenic_ddr_priv *p, u32 off, u32 mask, u32 val)
{
	u32 v = a1p_r(p, off);

	v &= ~mask;
	v |= val & mask;
	a1p_w(p, off, v);
}

/*
 * A1 CGU: ungate the DDR clock (CLKGR0 bit 3) and re-latch the DDRCDR
 * divider off the (pll_init-reprogrammed) MPLL. A1 leaves the clock
 * source bits to the bootrom (no source-mux write), and its DDRCDR is
 * at CPM+0x3c, so it cannot share the T40/T41 ingenic_ddr_cgu_init().
 * Divider = MPLL/DDR - 1 (1 for the MPLL/2 setpoint all A1 SKUs use).
 */
void ingenic_ddr_a1_cgu_init(const struct ingenic_ddr_variant *v)
{
	void __iomem *clkgr0 = (void __iomem *)(uintptr_t)(A1_CPM_BASE + A1_CPM_CLKGR0);
	void __iomem *ddrcdr = (void __iomem *)(uintptr_t)(A1_CPM_BASE + A1_CPM_DDRCDR);
	u32 cdr = (v->mpll_hz / v->ddr_hz) - 1;
	u32 r;

	r = readl(clkgr0);
	r &= ~A1_CPM_CLKGR0_DDR;
	writel(r, clkgr0);

	r = readl(ddrcdr);
	r &= ~(0xf | (0x3fu << 24));		/* clear divider + CE/BUSY/STOP */
	r |= (1u << 29) | (cdr & 0xf);		/* CE | divider */
	writel(r, ddrcdr);
	while (readl(ddrcdr) & (1u << 28))	/* wait BUSY */
		;
}

/* Drive strength / ODT / DQS / per-DQ-bit / VREF (vendor ddr_param_write).
 * All values come from the per-SKU a1_phy sub-struct; the register indices
 * are the vendor DQxRxOFFSET / drive / ODT tables. */
static void a1_phy_param_write(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;
	static const u32 odt_pd_idx[] = { 0x140, 0x150, 0x160, 0x170 };
	static const u32 odt_pu_idx[] = { 0x141, 0x151, 0x161, 0x171 };
	static const u32 drv_a_pd[] = { 0x142, 0x152 };
	static const u32 drv_a_pu[] = { 0x143, 0x153 };
	static const u32 drv_b_pd[] = { 0x162, 0x172 };
	static const u32 drv_b_pu[] = { 0x163, 0x173 };
	static const u32 dqs_a_idx[] = { 0x1d2, 0x1e7, 0x1ea, 0x1eb };
	static const u32 dqs_b_idx[] = { 0x232, 0x247, 0x24a, 0x24b };
	static const u32 vref_idx[] = { 0x147, 0x157, 0x167, 0x177 };
	/* per-DQ-bit delay offsets (vendor DQxRxOFFSET), added to the
	 * channel-A (0x1c0) and channel-B (0x220) bases. */
	static const u32 dq_off[16] = {
		0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10,
		0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x23, 0x25,
	};
	int i;

	for (i = 0; i < 4; i++) {
		a1p_idx(p, odt_pd_idx[i], v->a1_phy.odt_pd);
		a1p_idx(p, odt_pu_idx[i], v->a1_phy.odt_pu);
	}

	a1p_idx(p, 0x130, v->a1_phy.drvcmd_pd);
	a1p_idx(p, 0x131, v->a1_phy.drvcmd_pu);
	a1p_idx(p, 0x132, v->a1_phy.drvcmdck_pd);
	a1p_idx(p, 0x133, v->a1_phy.drvcmdck_pu);

	for (i = 0; i < 2; i++) {
		a1p_idx(p, drv_a_pd[i], v->a1_phy.dq_drv_a_pd);
		a1p_idx(p, drv_a_pu[i], v->a1_phy.dq_drv_a_pu);
		a1p_idx(p, drv_b_pd[i], v->a1_phy.dq_drv_b_pd);
		a1p_idx(p, drv_b_pu[i], v->a1_phy.dq_drv_b_pu);
	}

	for (i = 0; i < 4; i++) {
		a1p_idx(p, dqs_a_idx[i], v->a1_phy.dqs_a);
		a1p_idx(p, dqs_b_idx[i], v->a1_phy.dqs_b);
	}

	for (i = 0; i < 16; i++) {
		a1p_idx(p, 0x1c0 + dq_off[i], v->a1_phy.dq_a);
		a1p_idx(p, 0x220 + dq_off[i], v->a1_phy.dq_b);
	}

	/* VREF: vendor uses ddrp_reg_set_range(idx, 0, 8, vref) -> low byte. */
	for (i = 0; i < 4; i++)
		a1p_rmw(p, vref_idx[i] * 4, 0xff, v->a1_phy.vref);
}

/* PHY PLL + mode config (vendor ddrp_pll_init) followed by the per-SKU
 * drive/ODT/DQS/DQ/VREF programming. */
int ingenic_ddr_a1_phy_init(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;
	bool dw32 = (v->bus_width == 32);
	int timeout = 100000;

	a1p_rmw(p, A1P_MEM_CFG, 0xff, v->ddrp_memcfg);
	a1p_rmw(p, A1P_DQ_WIDTH, 0x0f, dw32 ? 0x0f : 0x03);
	a1p_rmw(p, A1P_INNO_PHY_RST, 0xff, 0x0d);
	a1p_rmw(p, A1P_CWL, 0x0f, v->ddrp_cwl);
	a1p_rmw(p, A1P_CL, 0x0f, v->ddrp_cl);
	a1p_w(p, A1P_AL, 0x00);
	a1p_rmw(p, A1P_PLL_PD, 0xff, 0x00);

	/*
	 * Innophy PLL band select is rate-dependent; the 650-900 MHz band
	 * (all A1 SKUs run DDR at 700/800 MHz) leaves PLL_CTRL/PLL_PD at
	 * their reset value, so nothing extra to program here.
	 */
	while (!(a1p_r(p, A1P_PLL_LOCK) & 0x4) && --timeout)
		;
	if (!timeout)
		return -1;

	a1_phy_param_write(p);
	return 0;
}

/* Hardware DQS calibration (vendor ddrp_hardware_calibration): pulse
 * TRAINING_CTRL bit0 and wait for all byte lanes to report done
 * (0xf for the 32-bit bus, 0x3 for 16-bit). */
int ingenic_ddr_a1_phy_hw_calibration(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;
	u32 done = (v->bus_width == 32) ? 0xf : 0x3;
	int timeout = 100000;
	u32 ctrl;

	a1p_w(p, A1P_TRAINING_CTRL, 0x00);
	a1p_r(p, A1P_TRAINING_CTRL);
	a1p_w(p, A1P_TRAINING_CTRL, 0x01);

	while ((a1p_r(p, A1P_CALIB_DONE) & 0xf) != done && --timeout)
		;
	if (!timeout)
		return -1;

	ctrl = a1p_r(p, A1P_TRAINING_CTRL);
	a1p_w(p, A1P_TRAINING_CTRL, ctrl & ~0x01u);
	return 0;
}

/* Vendor ddrp_set_rfifo() tail: MEM_CFG (reg 0x1) bit 5 + RFIFO (reg
 * 0xe = byte 0x38) low 3 bits = 3. Runs after the controller post-init/
 * autosr; PHY registers are not covered by the DDRC hreg/preg
 * write-protection so this is safe here. */
void ingenic_ddr_a1_post_phy_fixups(struct ingenic_ddr_priv *p)
{
	a1p_rmw(p, A1P_MEM_CFG, 1u << 5, 1u << 5);
	a1p_rmw(p, A1P_RFIFO, 0x7, 0x3);
}
