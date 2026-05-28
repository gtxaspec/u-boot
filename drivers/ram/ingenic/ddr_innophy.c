// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst2 DDR controller + Innophy PHY driver.
 *
 * Ported from vendor U-Boot T41-1.2.6 arch/mips/cpu/xburst2/
 * ddr_innophy.c. The init flow is preserved as-is - it is well-tested
 * in production silicon and any restructuring risks introducing
 * timing-margin regressions. What changes vs the vendor original:
 *
 *  * Wrapped as a UCLASS_RAM driver so it can be probed from DT.
 *  * Per-variant register tables are static C `struct ingenic_ddr_
 *    variant` entries (mirror the vendor ddr_params_creator output)
 *    instead of a build-time-generated <generated/ddr_reg_values.h>.
 *    A single driver binary covers every T41/T40 SKU.
 *  * Hardware DQS calibration is unconditional (CONFIG_DDR_HARDWARE_
 *    TRAINING was a vendor build switch; we always train).
 *  * Vendor's DDR_CHIP_0_SIZE / DDR_CHIP_1_SIZE / REMMAP_ARRAY macros
 *    become fields on the variant struct.
 *
 * Probes via the UCLASS_RAM uclass off DT in both SPL and U-Boot
 * proper. SPL's probe runs ingenic_ddr_sdram_init() to actually
 * bring DRAM up; U-Boot proper's probe skips the init (SPL already
 * did it) and just records the size for ram_get_info() consumers.
 */

#define LOG_CATEGORY UCLASS_RAM

#include <dm.h>
#include <hang.h>
#include <log.h>
#include <ram.h>
#include <asm/io.h>
#include <dm/device-internal.h>
#include <dm/device_compat.h>
#include <dm/lists.h>
#include <linux/delay.h>
#include <linux/err.h>

#include "ddr_innophy.h"

/* ------------------------------------------------------------------
 * Vendor sdram_init helpers (transcribed from xburst2/ddr_innophy.c).
 * Static; only ingenic_ddr_sdram_init() is exported.
 * ------------------------------------------------------------------ */

static void ddrc_reset_phy(struct ingenic_ddr_priv *p)
{
	ddr_writel(p, 0xf << 20, DDRC_CTRL);
	mdelay(1);
	ddr_writel(p, 0x8 << 20, DDRC_CTRL);	/* dfi_reset_n low for Innophy */
	mdelay(1);
}

static void ddrc_prev_init(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;
	int i;

	for (i = 0; i < 5; i++)
		ddr_writel(p, v->ddrc_timing[i], DDRC_TIMING(i + 1));

	ddr_writel(p, v->ddrc_mmap0, DDRC_MMAP0);
	ddr_writel(p, v->ddrc_mmap1, DDRC_MMAP1);

	/* CKE off during prev_init; ddrc_post_init enables it. */
	ddr_writel(p, v->ddrc_ctrl & ~(7u << 12), DDRC_CTRL);
}

static void ddrc_post_init(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;
	int i;

	ddr_writel(p, v->ddrc_refcnt, DDRC_REFCNT);

	if (v->type == INGENIC_DDR_TYPE_DDR3) {
		for (i = 0; i < 5; i++)
			ddr_writel(p, v->remap[i], DDRC_REMAP(i + 1));
	}

	ddr_writel(p, v->ddrc_ctrl, DDRC_CTRL);
	ddr_writel(p, v->ddrc_cguc0, DDRC_CGUC0);
	ddr_writel(p, v->ddrc_cguc1, DDRC_CGUC1);
}

static void ddrc_dfi_init(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;
	u32 dlmr = v->ddrc_dlmr;
	u32 kgd_rtt_dic = v->par[IDP_KGD_RTT_DIC];

	/* Bring DFI up and program controller CFG */
	ddr_writel(p, DDRC_DWCFG_DFI_INIT_START, DDRC_DWCFG);
	ddr_writel(p, 0, DDRC_DWCFG);
	while (!(ddr_readl(p, DDRC_DWSTATUS) & DDRC_DWSTATUS_DFI_INIT_COMP))
		;

	ddr_writel(p, 0, DDRC_CTRL);			/* dfi_reset_n high */
	udelay(5);
	ddr_writel(p, v->ddrc_cfg, DDRC_CFG);
	udelay(5);
	ddr_writel(p, DDRC_CTRL_CKE, DDRC_CTRL);	/* CKE high */
	udelay(5);

	/* Type-specific MR programming. */
	switch (v->type) {
	case INGENIC_DDR_TYPE_DDR2: {
#define MR_DDR2(mr) (DDRC_LMR_START | DDRC_LMR_CMD_LMR | (1 << 1) | \
		     (((mr) & 0x1fff) << DDRC_LMR_DDR_ADDR_BIT) | \
		     ((((mr) >> 13) & 0x3) << DDRC_LMR_BA_BIT))
		while (ddr_readl(p, DDRC_LMR) & DDRC_LMR_START)
			;
		ddr_writel(p, 0x400003, DDRC_LMR);
		udelay(100);
		ddr_writel(p, MR_DDR2(v->mr2), DDRC_LMR);
		udelay(5);
		ddr_writel(p, MR_DDR2(v->mr3), DDRC_LMR);
		udelay(5);
		ddr_writel(p, (MR_DDR2(v->mr1) & ~0x46000) | (kgd_rtt_dic << 12),
			   DDRC_LMR);
		udelay(5);
		ddr_writel(p, MR_DDR2(v->mr0), DDRC_LMR);
		ddr_writel(p, 0x400003, DDRC_LMR);
		udelay(10);
		ddr_writel(p, 0x43, DDRC_LMR);
		udelay(10);
		ddr_writel(p, 0x43, DDRC_LMR);
		udelay(10);
		ddr_writel(p, 0xa73083, DDRC_LMR);
		ddr_writel(p, 0x384283, DDRC_LMR);
		ddr_writel(p, 0x4283, DDRC_LMR);
		ddr_writel(p, 0xc3, DDRC_LMR);
		udelay(500);
#undef MR_DDR2
		break;
	}

	case INGENIC_DDR_TYPE_DDR3: {
#define MR_DDR3(mr) (dlmr | DDRC_LMR_START | DDRC_LMR_CMD_LMR | \
		     (((mr) & 0xffff) << DDRC_LMR_DDR_ADDR_BIT) | \
		     ((((mr) >> 16) & 0x7) << DDRC_LMR_BA_BIT))
		ddr_writel(p, 0, DDRC_LMR); udelay(5);
		ddr_writel(p, MR_DDR3(v->mr2), DDRC_LMR);
		udelay(5);
		ddr_writel(p, 0, DDRC_LMR); udelay(5);
		ddr_writel(p, MR_DDR3(v->mr3), DDRC_LMR);
		udelay(5);
		ddr_writel(p, 0, DDRC_LMR); udelay(5);
		ddr_writel(p, (MR_DDR3(v->mr1) & ~0x266000) | (kgd_rtt_dic << 12),
			   DDRC_LMR);
		udelay(5);
		ddr_writel(p, 0, DDRC_LMR); udelay(5);
		ddr_writel(p, MR_DDR3(v->mr0), DDRC_LMR);
		udelay(5);
		ddr_writel(p, dlmr | DDRC_LMR_START | DDRC_LMR_CMD_ZQCL_CS0,
			   DDRC_LMR);
		udelay(5);
#undef MR_DDR3
		break;
	}

	case INGENIC_DDR_TYPE_LPDDR2: {
#define MR_LP(mr) ((1 << 1) | dlmr | DDRC_LMR_START | DDRC_LMR_CMD_LMR | \
		   (((mr) & 0xff) << 24) | \
		   ((((mr) >> 8) & 0xff) << 16))
		ddr_writel(p, MR_LP(v->mr63), DDRC_LMR);
		ddr_writel(p, MR_LP(v->mr10), DDRC_LMR);
		ddr_writel(p, MR_LP(v->mr1),  DDRC_LMR);
		ddr_writel(p, MR_LP(v->mr2),  DDRC_LMR);
		ddr_writel(p, MR_LP(v->mr3),  DDRC_LMR);
#undef MR_LP
		break;
	}

	case INGENIC_DDR_TYPE_LPDDR3: {
#define MR_LP3(mr) (dlmr | DDRC_LMR_START | DDRC_LMR_CMD_LMR | \
		    (((mr) & 0xff) << 24) | \
		    ((((mr) >> 8) & 0xff) << 16))
		ddr_writel(p, MR_LP3(v->mr63), DDRC_LMR); udelay(10);
		ddr_writel(p, MR_LP3(v->mr10), DDRC_LMR); udelay(10);
		ddr_writel(p, MR_LP3(v->mr1),  DDRC_LMR); udelay(10);
		ddr_writel(p, MR_LP3(v->mr2),  DDRC_LMR); udelay(10);
		ddr_writel(p, MR_LP3(v->mr3),  DDRC_LMR); udelay(10);
		/* MR11 also fired by vendor; reuse mr10 slot if needed. */
#undef MR_LP3
		break;
	}

	default:
		log_err("ingenic-ddr: unsupported type %d\n", v->type);
		hang();
	}
}

static void ddrc_autosr_setup(struct ingenic_ddr_priv *p)
{
	const struct ingenic_ddr_variant *v = p->cfg;

	ddr_writel(p, v->ddrc_autosr_cnt, DDRC_AUTOSR_CNT);
	ddr_writel(p, v->ddrc_autosr_en ? 1 : 0, DDRC_AUTOSR_EN);

	/* Vendor reads back two regs for ordering. */
	(void)ddr_readl(p, DDRC_AUTOSR_CNT);
	(void)ddr_readl(p, DDRC_DWCFG);

	ddr_writel(p, v->ddrc_hregpro, DDRC_HREGPRO);
	ddr_writel(p, v->ddrc_pregpro, DDRC_PREGPRO);
}

/* ------------------------------------------------------------------
 * Top-level init - matches vendor sdram_init() phase ordering.
 * ------------------------------------------------------------------ */

/* CGU DDR clock divider. Programs the controller's input clock to
 * the variant's ddr_hz before the PHY PLL setup; the Innophy PHY
 * takes the CGU output as its reference clock and ddr_phy_init()
 * assumes that input is at ddr_hz. Matches vendor clk_set_rate(DDR,
 * ddrfreq): cdr = mpll/ddr - 1. */
#define CPM_BASE_KSEG1		0xb0000000u
#define CPM_DDRCDR_OFFSET	0x2cu

static void ingenic_ddr_cgu_init(const struct ingenic_ddr_variant *v)
{
	void __iomem *ddrcdr = (void __iomem *)(CPM_BASE_KSEG1 + CPM_DDRCDR_OFFSET);
	u32 cdr = (v->mpll_hz / v->ddr_hz) - 1;
	u32 r;

	r = readl(ddrcdr);
	r &= ~(3u << 30);
	r |= (2u << 30);		/* source = MPLL */
	writel(r, ddrcdr);

	r = readl(ddrcdr);
	r &= ~(0xf | (0x3fu << 24));
	r |= (1u << 29) | (cdr & 0xf);	/* CE | divider */
	writel(r, ddrcdr);
	while (readl(ddrcdr) & (1u << 28))
		;
}

int ingenic_ddr_sdram_init(struct ingenic_ddr_priv *p)
{
	int ret;

	debug("ingenic-ddr: init %s (%s, %u MHz)\n",
	      p->cfg->name, p->cfg->chip, p->cfg->ddr_hz / 1000000);

	ingenic_ddr_cgu_init(p->cfg);

	ddrc_reset_phy(p);

	ret = ingenic_ddr_phy_init(p);
	if (ret)
		return ret;

	ddrc_dfi_init(p);

	ingenic_ddr_phy_set_drv_odt(p);
	ddrc_prev_init(p);

	ret = ingenic_ddr_phy_hw_calibration(p);
	if (ret)
		return ret;

	ddrc_post_init(p);
	ddrc_autosr_setup(p);

	/* Optimize DDR bandwidth (vendor magic writes at end of init). */
	writel(0x0,         (void __iomem *)(0xb301206cu));
	writel(0xff000000,  (void __iomem *)(0xb3012040u));
	writel(0x2bd07460,  (void __iomem *)(0xb3012048u));
	writel(0x1,         (void __iomem *)(0xb301206cu));

	ingenic_ddr_phy_set_vref_skew(p);

	return 0;
}

/* ------------------------------------------------------------------
 * UCLASS_RAM driver. Probes off DT in both SPL (DDR bring-up) and
 * U-Boot proper (just records DRAM size for ram_get_info() consumers,
 * because SPL already brought the controller up).
 *
 * Phase split: ingenic_ddr_sdram_init() is the actual init work, run
 * exactly once per cold-boot from the SPL probe. The proper-side
 * probe skips it - DRAM is already alive.
 * ------------------------------------------------------------------ */

static int ingenic_ddr_probe(struct udevice *dev)
{
	struct ingenic_ddr_priv *p = dev_get_priv(dev);
	const struct ingenic_ddr_variant *v;
	fdt_addr_t base;
	u64 size;

	v = (const struct ingenic_ddr_variant *)dev_get_driver_data(dev);
	if (!v) {
		dev_err(dev, "no variant config bound to compatible\n");
		return -ENODEV;
	}
	p->cfg = v;

	base = dev_read_addr(dev);
	if (base == FDT_ADDR_T_NONE)
		return -EINVAL;
	p->base = (void __iomem *)(uintptr_t)base;

	size = (u64)v->chip0_size + (u64)v->chip1_size;
	/* Cap at 512 MiB - the bus-addressable low region on XBurst2. */
	if (size > 0x20000000ULL)
		size = 0x20000000ULL;
	p->ram_size = (u32)size;

	/* SPL: actually bring DRAM up here.
	 * U-Boot proper: SPL already did it - just leave ram_size set. */
	if (IS_ENABLED(CONFIG_XPL_BUILD)) {
		int ret = ingenic_ddr_sdram_init(p);

		if (ret) {
			dev_err(dev, "sdram_init failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int ingenic_ddr_get_info(struct udevice *dev, struct ram_info *info)
{
	struct ingenic_ddr_priv *p = dev_get_priv(dev);

	info->base = 0;
	info->size = p->ram_size;
	return 0;
}

static const struct ram_ops ingenic_ddr_ops = {
	.get_info = ingenic_ddr_get_info,
};

static const struct udevice_id ingenic_ddr_ids[] = {
	{ .compatible = "ingenic,t41a-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41a },
	{ .compatible = "ingenic,t41l-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41l },
	{ .compatible = "ingenic,t41lq-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41lq },
	{ .compatible = "ingenic,t41n-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41n },
	{ .compatible = "ingenic,t41nq-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41nq },
	{ .compatible = "ingenic,t41xq-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41xq },
	{ .compatible = "ingenic,t41zg-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41zg },
	{ .compatible = "ingenic,t41zgc-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41zgc },
	{ .compatible = "ingenic,t41zl-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41zl },
	{ .compatible = "ingenic,t41zm-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41zm },
	{ .compatible = "ingenic,t41zmc-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41zmc },
	{ .compatible = "ingenic,t41zn-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41zn },
	{ .compatible = "ingenic,t41zx-ddr-innophy",
	  .data = (ulong)&ingenic_ddr_variant_t41zx },
	{ }
};

U_BOOT_DRIVER(ingenic_ddr_innophy) = {
	.name		= "ingenic_ddr_innophy",
	.id		= UCLASS_RAM,
	.of_match	= ingenic_ddr_ids,
	.ops		= &ingenic_ddr_ops,
	.probe		= ingenic_ddr_probe,
	.priv_auto	= sizeof(struct ingenic_ddr_priv),
};
