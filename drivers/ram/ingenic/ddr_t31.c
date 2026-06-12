// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 (XBurst1) DDR2/DDR3 controller + Innophy PHY init.
 *
 * UCLASS_RAM driver, sibling of the XBurst2 ddr_innophy.c (different IP,
 * different register layout). Faithful transliteration of the vendor
 * known-good path (arch/mips/cpu/xburst/ddr_innophy.c, ddr_set_dll.c,
 * clk.c), formerly arch/mips/mach-xburst/t31/sdram.c. The per-SKU
 * register/clock values now come from the DT-selected
 * struct ingenic_t31_ddr_variant (of_match .data) instead of
 * compile-time CONFIG_T31_VARIANT_* #if branches.
 *
 * Probes off DT in both SPL (DDR bring-up) and U-Boot proper (just
 * records DRAM size for ram_get_info()). The register write order, poll
 * loops and delays are timing-critical and reproduced exactly from the
 * vendor source.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#define LOG_CATEGORY UCLASS_RAM

#include <dm.h>
#include <log.h>
#include <ram.h>
#include <fdtdec.h>
#include <dm/device_compat.h>
#include <linux/delay.h>
#include <linux/libfdt.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/global_data.h>
#include <mach/t31.h>
#include "ddr_t31.h"

DECLARE_GLOBAL_DATA_PTR;

#define ddr_writel(v, reg)	writel((v), (void __iomem *)(DDRC_BASE + (reg)))
#define ddr_readl(reg)		readl((void __iomem *)(DDRC_BASE + (reg)))
#define phy_writel(v, reg)	writel((v), (void __iomem *)(DDR_PHY_BASE + (reg)))
#define phy_readl(reg)		readl((void __iomem *)(DDR_PHY_BASE + (reg)))

static u32 cpm_readl(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void cpm_writel(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

/*
 * WARNING: DDR CLK GATE (CPM_DRCG 0xb00000d0) BIT6 must stay set (0x40),
 * otherwise chip memory is not stable and the GPU hangs.
 */
static void reset_dll(void)
{
	cpm_writel(0x73 | (1 << 6), CPM_DRCG);
	mdelay(1);
	cpm_writel(0x71 | (1 << 6), CPM_DRCG);
	mdelay(1);
}

/*
 * DDR clock divider. The T31 DDR clock is always MPLL/2 on every
 * variant, so the CPM_DDRCDR divider (cdr) is 1. Force the source to
 * MPLL (the 2-bit field at 31:30 = 2): trusting the bootrom is unsafe -
 * if it left the source on APLL the cdr=1 divider would overclock the
 * DDR and randomly corrupt memory.
 */
static void ddr_clk_set_rate(const struct ingenic_t31_ddr_variant *cfg)
{
	u32 cdr = cfg->ddr_cdr ? cfg->ddr_cdr : 1;	/* 0 = default MPLL/2 */
	u32 regval = cpm_readl(CPM_DDRCDR);

	regval &= ~(0xf | (0x3f << 24));
	regval &= ~(0x3 << 30);
	regval |= (0x2 << 30);
	regval |= ((1 << 29) | cdr);		/* change-enable (bit 29) + divider */
	cpm_writel(regval, CPM_DDRCDR);
	while (cpm_readl(CPM_DDRCDR) & (1 << 28))	/* busy = bit 28 */
		;
}

static void reset_controller(void)
{
	ddr_writel(0xf << 20, DDRC_CTRL);
	mdelay(5);
	ddr_writel(0, DDRC_CTRL);
	mdelay(5);
}

static void remap_swap(int a, int b)
{
	u32 remmap[2], tmp[2];

	remmap[0] = ddr_readl(DDRC_REMAP(a / 4 + 1));
	remmap[1] = ddr_readl(DDRC_REMAP(b / 4 + 1));

#define BIT_OF(bit) (((bit) % 4) * 8)
#define MASK_OF(bit) (0x1f << BIT_OF(bit))
	tmp[0] = (remmap[0] & MASK_OF(a)) >> BIT_OF(a);
	tmp[1] = (remmap[1] & MASK_OF(b)) >> BIT_OF(b);

	remmap[0] &= ~MASK_OF(a);
	remmap[1] &= ~MASK_OF(b);

	ddr_writel(remmap[0] | (tmp[1] << BIT_OF(a)), DDRC_REMAP(a / 4 + 1));
	ddr_writel(remmap[1] | (tmp[0] << BIT_OF(b)), DDRC_REMAP(b / 4 + 1));
#undef BIT_OF
#undef MASK_OF
}

static void mem_remap(const struct ingenic_t31_ddr_variant *cfg)
{
	u32 start = 0, num = 0;
	int row, col, dw32, bank8, cs0, cs1;

	row = cfg->row;
	col = cfg->col;
	dw32 = cfg->dw32;
	bank8 = cfg->bank8;
	cs0 = cfg->cs0;
	cs1 = cfg->cs1;

	start += row + col + (dw32 ? 4 : 2) / 2;
	start -= 12;

	if (bank8)
		num += 3;
	else
		num += 2;

	if (cs0 && cs1)
		num++;

	for (; num > 0; num--)
		remap_swap(0 + num - 1, start + num - 1);
}

static void ddr_controller_init(const struct ingenic_t31_ddr_variant *cfg)
{
	ddr_writel(DDRC_CTRL_CKE | DDRC_CTRL_ALH, DDRC_CTRL);
	ddr_writel(0, DDRC_CTRL);

	ddr_writel(cfg->ddrc_cfg, DDRC_CFG);

	ddr_writel(cfg->ddrc_timing[0], DDRC_TIMING(1));
	ddr_writel(cfg->ddrc_timing[1], DDRC_TIMING(2));
	ddr_writel(cfg->ddrc_timing[2], DDRC_TIMING(3));
	ddr_writel(cfg->ddrc_timing[3], DDRC_TIMING(4));
	ddr_writel(cfg->ddrc_timing[4], DDRC_TIMING(5));
	ddr_writel(cfg->ddrc_timing[5], DDRC_TIMING(6));

	ddr_writel(cfg->ddrc_mmap0, DDRC_MMAP0);
	ddr_writel(cfg->ddrc_mmap1, DDRC_MMAP1);
	ddr_writel(DDRC_CTRL_CKE | DDRC_CTRL_ALH, DDRC_CTRL);
	ddr_writel(cfg->ddrc_refcnt, DDRC_REFCNT);
	ddr_writel(cfg->ddrc_ctrl & 0xffff8fff, DDRC_CTRL);
}

/*
 * RX DQS window calibration (DDR2 path, the #if 1 branch of the vendor
 * phy_calibration()).
 */
static void phy_calibration(void)
{
	int m;

	m = phy_readl(INNO_TRAINING_CTRL);
	m = 0xa1;
	phy_writel(m, INNO_TRAINING_CTRL);
	while (0x3 != readl((void __iomem *)(DDR_PHY_BASE + 0xcc)))
		;
	phy_writel(0xa0, INNO_TRAINING_CTRL);
}

static void ddr_inno_phy_init(const struct ingenic_t31_ddr_variant *cfg)
{
	bool ddr3 = (cfg->type == T31_DDR_TYPE_DDR3);
	u32 reg = 0;

	phy_writel(0x14, INNO_PLL_FBDIV);
	phy_writel(0x1a, INNO_PLL_CTRL);
	phy_writel(0x5, INNO_PLL_PDIV);
	phy_writel(0x18, INNO_PLL_CTRL);

	while (!(phy_readl(INNO_PLL_LOCK) & (1 << 3)))	/* wait pll lock */
		;

	phy_writel(0x0, INNO_TRAINING_CTRL);
	phy_writel(0x03, INNO_DQ_WIDTH);

	if (ddr3) {
		/* MEMSEL = DDR3, BURSEL = burst8 */
		phy_writel(0x30, INNO_MEM_CFG);
		/* DQS0/1 TXPLL: clear [6:4] (vendor non-T23 path) */
		phy_writel(phy_readl(0x154) & 0xffffff8f, 0x154);
		phy_writel(phy_readl(0x114) & 0xffffff8f, 0x114);
		phy_writel(0x0d, INNO_CHANNEL_EN);
		phy_writel(0x6, INNO_CWL);
		phy_writel(0x8, INNO_CL);
		phy_writel(0x00, INNO_AL);
	} else {
		/* MEMSEL = DDR2, BURSEL = burst8 */
		phy_writel(0x11, INNO_MEM_CFG);
		phy_writel(0x0d, INNO_CHANNEL_EN);
		phy_writel(((cfg->mr0 & 0xf0) >> 4) - 1, INNO_CWL);
		reg = ((cfg->mr0 & 0xf0) >> 4);
		phy_writel(reg, INNO_CL);
		phy_writel(0x00, INNO_AL);
	}

	writel(0, (void __iomem *)DDR_APB_PHY_INIT);	/* start high */
	while (!(readl((void __iomem *)DDR_APB_PHY_INIT) & (1 << 2)))	/* pll locked */
		;
	writel(0, (void __iomem *)REG_DDR_CTRL);

	while (!(readl((void __iomem *)DDR_APB_PHY_INIT) & (1 << 1)))	/* init_complete */
		;
	while (!readl((void __iomem *)T31_INIT_COMP))
		;
	writel(0, (void __iomem *)REG_DDR_CTRL);

	if (ddr3) {
		/* DDR3: DFI reset (kgdreset) - set, 200us, clear, 500us. */
		writel(DDRC_CTRL_DFI_RST, (void __iomem *)REG_DDR_CTRL);
		udelay(200);
		writel(0, (void __iomem *)REG_DDR_CTRL);
		udelay(500);
	}

	writel(cfg->ddrc_cfg, (void __iomem *)REG_DDR_CFG);
	writel(0x0a, (void __iomem *)REG_DDR_CTRL);

	if (ddr3) {
		/* DDR3 LMR MRS sequence: MR2,MR3,MR1,MR0,ZQCL (no-poll
		 * writel pairs - vendor ddr_innophy.c DDR3 branch). */
		writel((0x08 << 12) | 0x211, (void __iomem *)REG_DDR_LMR);
		writel(0, (void __iomem *)REG_DDR_LMR);
		writel(0x311, (void __iomem *)REG_DDR_LMR);
		writel(0, (void __iomem *)REG_DDR_LMR);
		writel((0x6 << 12) | 0x111, (void __iomem *)REG_DDR_LMR);
		writel(0, (void __iomem *)REG_DDR_LMR);
		writel((cfg->mr0 << 12) | 0x011, (void __iomem *)REG_DDR_LMR);
		writel(0, (void __iomem *)REG_DDR_LMR);
		writel(0x19, (void __iomem *)REG_DDR_LMR);
		writel(0, (void __iomem *)REG_DDR_LMR);

		/* DDR3 hardware write-leveling (wait WL_DONE == 0x3). */
		writel(0x4, (void __iomem *)(DDR_PHY_BASE + 0x0c));
		writel(0x40, (void __iomem *)(DDR_PHY_BASE + 0x10));
		writel(0xa4, (void __iomem *)(DDR_PHY_BASE + 0x08));
		while (0x3 != readl((void __iomem *)(DDR_PHY_BASE + 0xc0)))
			;
		writel(0xa1, (void __iomem *)(DDR_PHY_BASE + 0x08));
	} else {
		writel(0x400001, (void __iomem *)REG_DDR_LMR);
		while ((0x1 & readl((void __iomem *)REG_DDR_LMR)) == 1)
			;

		writel(0x211, (void __iomem *)REG_DDR_LMR);
		while ((0x1 & readl((void __iomem *)REG_DDR_LMR)) == 1)
			;

		writel(0x311, (void __iomem *)REG_DDR_LMR);
		while ((0x1 & readl((void __iomem *)REG_DDR_LMR)) == 1)
			;

		writel(0x111, (void __iomem *)REG_DDR_LMR);
		while ((0x1 & readl((void __iomem *)REG_DDR_LMR)) == 1)
			;

		reg = (cfg->mr0 << 12) | 0x011;
		writel(reg, (void __iomem *)REG_DDR_LMR);
		while ((0x1 & readl((void __iomem *)REG_DDR_LMR)) == 1)
			;
		udelay(5 * 1000);

		writel(0x400001, (void __iomem *)REG_DDR_LMR);
		while ((0x1 & readl((void __iomem *)REG_DDR_LMR)) == 1)
			;

		writel(0x400009, (void __iomem *)REG_DDR_LMR);
		while ((0x1 & readl((void __iomem *)REG_DDR_LMR)) == 1)
			;

		writel(0x400009, (void __iomem *)REG_DDR_LMR);
		while ((0x1 & readl((void __iomem *)REG_DDR_LMR)) == 1)
			;
		udelay(5 * 1000);
	}

	phy_calibration();

	if (ddr3)
		writel(0x50, (void __iomem *)(DDR_PHY_BASE + 0x004));
	else
		writel(0x51, (void __iomem *)(DDR_PHY_BASE + 0x004));
	writel(0x24, (void __iomem *)(DDR_PHY_BASE + 0x028));

	while (((readl((void __iomem *)(DDR_PHY_BASE + 0x190)) & 0xe0) <= 0x20) &&
	       ((readl((void __iomem *)(DDR_PHY_BASE + 0x194)) & 0xe0) <= 0x20)) {
		writel((readl((void __iomem *)(DDR_PHY_BASE + 0x04)) | 0x40),
		       (void __iomem *)(DDR_PHY_BASE + 0x04));
		writel(((readl((void __iomem *)(DDR_PHY_BASE + 0x28)) & ~(0xe)) | 0x6),
		       (void __iomem *)(DDR_PHY_BASE + 0x28));
		break;
	}
}

/* Top-level DDR2/DDR3 init (innophy path of the vendor sdram_init()). */
int ingenic_t31_ddr_sdram_init(const struct ingenic_t31_ddr_variant *cfg)
{
	ddr_clk_set_rate(cfg);
	reset_dll();

	reset_controller();

	ddr_inno_phy_init(cfg);

	ddr_controller_init(cfg);

	/* open remap function */
	mem_remap(cfg);
	/* must modify after opening remap function */
	ddr_writel(cfg->ddrc_ctrl & 0xffff07ff, DDRC_CTRL);

	ddr_writel(ddr_readl(DDRC_STATUS) & ~DDRC_DSTATUS_MISS, DDRC_STATUS);
	ddr_writel(0, DDRC_DLP);

	return 0;
}

/*
 * SPL helper for t31/pll.c: find the T31 DDR node in the FDT by trying
 * each known per-SKU compatible, and hand back that SKU's PLL setpoints.
 * Shares the variant table below; runs before driver model is up.
 */
static const struct {
	const char *compatible;
	const struct ingenic_t31_ddr_variant *cfg;
} t31_ddr_variants[] = {
	{ "ingenic,t31x-ddr-innophy",  &ingenic_t31_ddr_variant_t31x   },
	{ "ingenic,t31n-ddr-innophy",  &ingenic_t31_ddr_variant_t31n   },
	{ "ingenic,t31l-ddr-innophy",  &ingenic_t31_ddr_variant_t31l   },
	{ "ingenic,t31lc-ddr-innophy", &ingenic_t31_ddr_variant_t31lc  },
	{ "ingenic,t31al-ddr-innophy", &ingenic_t31_ddr_variant_t31al  },
	{ "ingenic,t31a-ddr-innophy",  &ingenic_t31_ddr_variant_t31a   },
	{ "ingenic,c100-ddr-innophy",  &ingenic_t31_ddr_variant_c100   },
	{ "ingenic,t23n-ddr-innophy",  &ingenic_t31_ddr_variant_t23n   },
	{ "ingenic,t23zn-ddr-innophy", &ingenic_t31_ddr_variant_t23n   },
	{ "ingenic,t23dl-ddr-innophy", &ingenic_t31_ddr_variant_t23dl  },
	{ "ingenic,t23nhp-ddr-innophy", &ingenic_t31_ddr_variant_t23n_hp },
	{ "ingenic,t23nlp-ddr-innophy", &ingenic_t31_ddr_variant_t23n_lp },
	{ "ingenic,t23x-ddr-innophy",  &ingenic_t31_ddr_variant_t23x   },
	{ "ingenic,t23dn-ddr-innophy", &ingenic_t31_ddr_variant_t23dn  },
};

int ingenic_t31_ddr_pll_setpoints(u32 *apll_mnod, u32 *mpll_mnod, u32 *cpccr)
{
	const void *blob = gd->fdt_blob;
	int node, i;

	if (!blob)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(t31_ddr_variants); i++) {
		node = fdt_node_offset_by_compatible(blob, -1,
						     t31_ddr_variants[i].compatible);
		if (node >= 0) {
			*apll_mnod = t31_ddr_variants[i].cfg->apll_mnod;
			*mpll_mnod = t31_ddr_variants[i].cfg->mpll_mnod;
			*cpccr = t31_ddr_variants[i].cfg->cpccr;
			return 0;
		}
	}
	return -ENODEV;
}

/* ------------------------------------------------------------------
 * UCLASS_RAM driver. SPL probe brings DRAM up; U-Boot-proper probe just
 * records the size (DRAM is already alive). The per-SKU variant struct
 * comes from of_match .data (the node's compatible IS the selector).
 * ------------------------------------------------------------------ */

static int ingenic_t31_ddr_probe(struct udevice *dev)
{
	struct ingenic_t31_ddr_priv *p = dev_get_priv(dev);
	const struct ingenic_t31_ddr_variant *v;

	v = (const struct ingenic_t31_ddr_variant *)dev_get_driver_data(dev);
	if (!v) {
		dev_err(dev, "no variant data for compatible\n");
		return -ENODEV;
	}
	p->cfg = v;
	p->ram_size = v->chip0_size;

	if (IS_ENABLED(CONFIG_XPL_BUILD))
		return ingenic_t31_ddr_sdram_init(v);

	return 0;
}

static int ingenic_t31_ddr_get_info(struct udevice *dev, struct ram_info *info)
{
	struct ingenic_t31_ddr_priv *p = dev_get_priv(dev);

	info->base = 0;
	info->size = p->ram_size;
	return 0;
}

static const struct ram_ops ingenic_t31_ddr_ops = {
	.get_info = ingenic_t31_ddr_get_info,
};

/*
 * Per-SKU compatible + .data = variant struct (the upstream DM idiom for
 * per-variant data - the node's compatible selects the SKU; no
 * ingenic,variant string, no Kconfig variant).
 */
static const struct udevice_id ingenic_t31_ddr_ids[] = {
	{ .compatible = "ingenic,t31x-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t31x },
	{ .compatible = "ingenic,t31n-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t31n },
	{ .compatible = "ingenic,t31l-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t31l },
	{ .compatible = "ingenic,t31lc-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t31lc },
	{ .compatible = "ingenic,t31al-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t31al },
	{ .compatible = "ingenic,t31a-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t31a },
	{ .compatible = "ingenic,c100-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_c100 },
	/* T23: same XBurst1 legacy DDRC + Innophy DDR2 IP. */
	{ .compatible = "ingenic,t23n-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t23n },
	{ .compatible = "ingenic,t23zn-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t23n },
	{ .compatible = "ingenic,t23dl-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t23dl },
	{ .compatible = "ingenic,t23nhp-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t23n_hp },
	{ .compatible = "ingenic,t23nlp-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t23n_lp },
	{ .compatible = "ingenic,t23x-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t23x },
	{ .compatible = "ingenic,t23dn-ddr-innophy",
	  .data = (ulong)&ingenic_t31_ddr_variant_t23dn },
	{ }
};

U_BOOT_DRIVER(ingenic_t31_ddr) = {
	.name		= "ingenic_t31_ddr",
	.id		= UCLASS_RAM,
	.of_match	= ingenic_t31_ddr_ids,
	.ops		= &ingenic_t31_ddr_ops,
	.probe		= ingenic_t31_ddr_probe,
	.priv_auto	= sizeof(struct ingenic_t31_ddr_priv),
};
