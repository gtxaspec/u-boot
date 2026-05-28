/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic XBurst2 DDR controller + Innophy PHY register map.
 *
 * Bases are SoC-specific (T41 uses 0xb34f0000 for the controller); the
 * PHY and APB peripheral regions live at fixed offsets from the
 * controller base on every XBurst2 SoC that uses this IP:
 *   DDR_PHY_OFFSET  = -0x4e0000 + 0x1000  (i.e. 0xb3011000 on T41)
 *   DDR_APB_OFFSET  = -0x4e0000 + 0x2000  (i.e. 0xb3012000 on T41)
 *
 * Ported from vendor U-Boot T41-1.2.6 arch/mips/include/asm/
 * ddr_innophy.h.
 */

#ifndef _DRIVERS_RAM_INGENIC_DDR_INNOPHY_H
#define _DRIVERS_RAM_INGENIC_DDR_INNOPHY_H

#include <asm/io.h>
#include <linux/types.h>

#define DDR_PHY_OFFSET		(-0x4e0000 + 0x1000)
#define DDR_APB_OFFSET		(-0x4e0000 + 0x2000)

/* ----- DDRC controller (offsets from DDRC_BASE) ----- */
#define DDRC_STATUS		0x000
#define DDRC_CFG		0x008
#define DDRC_CTRL		0x010
#define DDRC_LMR		0x018
#define DDRC_DLP		0x020
#define DDRC_AUTOSR_EN		0x028
#define DDRC_AUTOSR_CNT		0x030
#define DDRC_REFCNT		0x038
#define DDRC_TIMING(n)		(0x040 + 8 * ((n) - 1))
#define DDRC_MMAP0		0x078
#define DDRC_MMAP1		0x080
#define DDRC_HREGPRO		0x0d8

/* DDRC CTRL bits */
#define DDRC_CTRL_CKE		(1 << 1)
#define DDRC_CTRL_ALH		(1 << 3)

/* DDRC LMR bits (matches vendor ddr_innophy.h - cmd at bit 6, BA at
 * bit 9, address at bit 12; this got transliterated wrong on the first
 * pass and led to bogus MR programming + training timeout). */
#define DDRC_LMR_START		(1 << 0)
#define DDRC_LMR_TMRD_BIT	1
#define DDRC_LMR_CMD_BIT	6
#define DDRC_LMR_CMD_PREC	(0 << DDRC_LMR_CMD_BIT)
#define DDRC_LMR_CMD_AUREF	(1 << DDRC_LMR_CMD_BIT)
#define DDRC_LMR_CMD_LMR	(2 << DDRC_LMR_CMD_BIT)
#define DDRC_LMR_CMD_ZQCL_CS0	(3 << DDRC_LMR_CMD_BIT)
#define DDRC_LMR_CMD_ZQCL_CS1	(4 << DDRC_LMR_CMD_BIT)
#define DDRC_LMR_BA_BIT		9
#define DDRC_LMR_DDR_ADDR_BIT	12
#define DDRC_LMR_MA_BIT		16	/* LPDDR2 MA[9:0] */
#define DDRC_LMR_OP_BIT		24	/* LPDDR2 OP[9:0] */

/* DDRC STATUS bits */
#define DDRC_STATUS_MISS	(1 << 6)

/* ----- DDRC APB peripheral region (offsets from DDRC_BASE) ----- */
#define DDRC_DWCFG		(DDR_APB_OFFSET + 0x00)
#define DDRC_DWSTATUS		(DDR_APB_OFFSET + 0x04)
#define DDRC_REMAP(n)		(DDR_APB_OFFSET + 0x08 + 4 * ((n) - 1))
#define DDRC_CGUC0		(DDR_APB_OFFSET + 0x64)
#define DDRC_CGUC1		(DDR_APB_OFFSET + 0x68)
#define DDRC_PREGPRO		(DDR_APB_OFFSET + 0x6c)

#define DDRC_DWCFG_DFI_INIT_START	(1 << 3)
#define DDRC_DWSTATUS_DFI_INIT_COMP	(1 << 0)

/* ----- DDR PHY (Innophy) registers (offsets from DDRC_BASE) ----- */
#define DDRP_INNO_PHY_RST	(DDR_PHY_OFFSET + 0x000)
#define DDRP_MEM_CFG		(DDR_PHY_OFFSET + 0x004)
#define DDRP_TRAINING_CTRL	(DDR_PHY_OFFSET + 0x008)
#define DDRP_CL			(DDR_PHY_OFFSET + 0x014)
#define DDRP_AL			(DDR_PHY_OFFSET + 0x018)
#define DDRP_CWL		(DDR_PHY_OFFSET + 0x01c)
#define DDRP_DQ_WIDTH		(DDR_PHY_OFFSET + 0x034)
#define DDRP_PLL_FBDIVL		(DDR_PHY_OFFSET + 0x140)
#define DDRP_PLL_FBDIVH		(DDR_PHY_OFFSET + 0x144)
#define DDRP_PLL_PDIV		(DDR_PHY_OFFSET + 0x148)
#define DDRP_PLL_CTRL		(DDR_PHY_OFFSET + 0x14c)
#define DDRP_PLL_LOCK		(DDR_PHY_OFFSET + 0x180)
#define DDRP_CALIB_DONE		(DDR_PHY_OFFSET + 0x184)
#define DDRP_INIT_COMP		(DDR_PHY_OFFSET + 0x110)

/* DQ width values for the 16-bit XBurst2 controller. */
#define DDRP_DQ_WIDTH_DQ_H	(1 << 1)
#define DDRP_DQ_WIDTH_DQ_L	(1 << 0)

/* PHY raw register base for free-form skew/VREF writes that don't have
 * a clean per-bit field layout in the Innophy spec. */
#define DDRP_RAW_BASE		(DDR_PHY_OFFSET + 0x000)

/* ----- DDR types (matches vendor enum ddr_type ordering) ----- */
enum ingenic_ddr_type {
	INGENIC_DDR_TYPE_DDR3 = 0,
	INGENIC_DDR_TYPE_LPDDR,
	INGENIC_DDR_TYPE_LPDDR2,
	INGENIC_DDR_TYPE_LPDDR3,
	INGENIC_DDR_TYPE_DDR2,
};

/*
 * Per-SoC PHY init family. The DDRC controller side is identical
 * across XBurst2 SoCs (same register set + offsets), but the Innophy
 * PHY init / drive-ODT / hardware calibration / per-bit skew paths
 * are SoC-family specific. T41 has full vendor-style efuse parameter
 * tables and HW-calibrated DQS; T40 boards use a different per-bit
 * skew layout, hard-coded drive/ODT, and an earlier DDR2 PHY init
 * sequence (DLL bypass, baseline TX delay seed, manual de-skew enable
 * before the PLL programming).
 */
enum ingenic_ddr_family {
	INGENIC_DDR_FAMILY_T41 = 0,	/* default for existing variants */
	INGENIC_DDR_FAMILY_T40,
	INGENIC_DDR_FAMILY_A1,
};

/* ----- Drive strength / ODT / skew / VREF efuse parameters -----
 * 19 numeric parameters per variant; vendor uses a flat array indexed
 * by these labels. Ports identically here so the SoC-specific config
 * tables can be transcribed straight from vendor efuse_ddr_get.c.
 */
enum ingenic_ddr_par {
	IDP_ODT_PD = 0,
	IDP_ODT_PU,
	IDP_CMD_RC_PD,
	IDP_CMD_RC_PU,
	IDP_CLK_RC_PD,
	IDP_CLK_RC_PU,
	IDP_DQX_RC_PD,
	IDP_DQX_RC_PU,
	IDP_VREF,
	IDP_KGD_ODT,
	IDP_KGD_DS,
	IDP_KGD_RTT_DIC,
	IDP_SKEW_DQS0R,
	IDP_SKEW_DQS1R,
	IDP_SKEW_DQRX,
	IDP_SKEW_DQS0T,
	IDP_SKEW_DQS1T,
	IDP_SKEW_DQTX,
	IDP_SKEW_TRX,
	IDP_NUM,
};

/* Per-variant DDR configuration. One struct per board/SoC variant,
 * matched at probe time by DT compatible string. All numeric fields
 * are direct vendor ddr_params_creator output (mirrors what we have
 * in arch/mips/mach-xburst/include/mach/t41<variant>-ddr.h). */
struct ingenic_ddr_variant {
	const char *name;		/* e.g. "T41NQ" */
	const char *chip;		/* e.g. "W631GU6NG" */
	enum ingenic_ddr_type type;
	enum ingenic_ddr_family family;	/* defaults to T41 (omit field) */
	unsigned int bus_width;		/* 16 or 32 */
	u32 chip0_size;
	u32 chip1_size;

	/* PLL / clocks */
	unsigned int mpll_hz;		/* expected MPLL freq, for clk_set_rate */
	unsigned int ddr_hz;		/* DDR clock target (= MPLL/2) */

	/* DDR controller values from ddr_params_creator */
	u32 ddrc_cfg;
	u32 ddrc_ctrl;
	u32 ddrc_dlmr;
	u32 ddrc_mmap0;
	u32 ddrc_mmap1;
	u32 ddrc_refcnt;
	u32 ddrc_timing[5];		/* 1..5 */
	u32 ddrc_autosr_cnt;
	u32 ddrc_autosr_en;
	u32 ddrc_hregpro;
	u32 ddrc_pregpro;
	u32 ddrc_cguc0;
	u32 ddrc_cguc1;

	/* PHY values from ddr_params_creator */
	u32 ddrp_memcfg;
	u32 ddrp_cl;
	u32 ddrp_cwl;

	/* Mode registers (DDR2/DDR3 use 0..3, LPDDR2/3 use 1..3,10,63) */
	u32 mr0, mr1, mr2, mr3, mr10, mr63;

	/* Memory remap array (5 words) - only used for DDR3 */
	u32 remap[5];

	/* Drive / ODT / skew / VREF efuse defaults (vendor
	 * efuse_ddr_get.c init_ddr_par). T41 family only. */
	unsigned int par[IDP_NUM];

	/* A1-family Innophy PHY tuning (vendor ddr3_param_t). The A1 PHY
	 * programs drive/ODT/DQS/DQ through a per-index register layout the
	 * T41 par[] table does not describe, so the A1 family carries its
	 * own field group; only ingenic_ddr_a1_phy_init() reads it. cl/cwl
	 * reuse ddrp_cl/ddrp_cwl, mem_cfg reuses ddrp_memcfg, and the MR1
	 * kgd ODT/DS patch is pre-baked into mr1 at transcription time.
	 * Compiled only into A1 builds so the T40/T41 variant tables keep
	 * their original size. */
#ifdef CONFIG_SOC_A1
	struct {
		u8 odt_pd, odt_pu;
		u8 drvcmd_pd, drvcmd_pu;
		u8 drvcmdck_pd, drvcmdck_pu;
		u8 dq_drv_a_pd, dq_drv_a_pu;
		u8 dq_drv_b_pd, dq_drv_b_pu;
		u8 dq_a, dq_b;
		u8 vref;
		u8 dqs_a, dqs_b;
	} a1_phy;
#endif
};

struct ingenic_ddr_priv {
	const struct ingenic_ddr_variant *cfg;
	void __iomem *base;		/* DDRC controller base */
	u32 ram_size;			/* total bytes (chip0+chip1, capped) */
};

/* ----- Inline accessors (DDRC_BASE-relative offsets) ----- */
static inline u32 ddr_readl(const struct ingenic_ddr_priv *p, u32 off)
{
	return readl(p->base + off);
}

static inline void ddr_writel(const struct ingenic_ddr_priv *p, u32 val, u32 off)
{
	writel(val, p->base + off);
}

/* ----- Driver-internal entry points (ddr_innophy_phy.c, T41 family) ----- */
int ingenic_ddr_phy_init(struct ingenic_ddr_priv *p);
int ingenic_ddr_phy_hw_calibration(struct ingenic_ddr_priv *p);
void ingenic_ddr_phy_set_drv_odt(struct ingenic_ddr_priv *p);
void ingenic_ddr_phy_set_vref_skew(struct ingenic_ddr_priv *p);

/* ----- T40-family PHY paths (ddr_innophy_phy_t40.c) ----- */
int ingenic_ddr_t40_phy_init(struct ingenic_ddr_priv *p);
int ingenic_ddr_t40_phy_hw_calibration(struct ingenic_ddr_priv *p);
void ingenic_ddr_t40_post_phy_fixups(struct ingenic_ddr_priv *p);
void ingenic_ddr_t40_phy_set_skew(struct ingenic_ddr_priv *p);

/* ----- A1-family PHY paths (ddr_innophy_phy_a1.c, A1 builds only) ----- */
#ifdef CONFIG_SOC_A1
void ingenic_ddr_a1_cgu_init(const struct ingenic_ddr_variant *v);
int ingenic_ddr_a1_phy_init(struct ingenic_ddr_priv *p);
int ingenic_ddr_a1_phy_hw_calibration(struct ingenic_ddr_priv *p);
void ingenic_ddr_a1_post_phy_fixups(struct ingenic_ddr_priv *p);
#endif

/* ----- Top-level init (ddr_innophy.c) ----- */
int ingenic_ddr_sdram_init(struct ingenic_ddr_priv *p);

/* ----- Per-variant configs (ddr_innophy_types.c, T41 family) ----- */
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41nq;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41a;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41l;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41lq;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41n;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41xq;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41zg;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41zgc;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41zl;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41zm;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41zmc;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41zn;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t41zx;

/* ----- Per-variant configs (T40 family) ----- */
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t40n;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_t40xp;

/* ----- Per-variant configs (A1 family, A1 builds only) ----- */
#ifdef CONFIG_SOC_A1
extern const struct ingenic_ddr_variant ingenic_ddr_variant_a1n;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_a1nt;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_a1x;
extern const struct ingenic_ddr_variant ingenic_ddr_variant_a1l;
#endif

#endif /* _DRIVERS_RAM_INGENIC_DDR_INNOPHY_H */
