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
#include <asm/global_data.h>
#include <asm/io.h>
#include <dm/device-internal.h>
#include <dm/device_compat.h>
#include <dm/lists.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/libfdt.h>

#include "ddr_innophy.h"

DECLARE_GLOBAL_DATA_PTR;

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

	/*
	 * Bring DFI up and program controller CFG. DWCFG bit 0 selects
	 * buswidth: 0=16-bit, 1=32-bit (matches vendor sdram_init T40
	 * DDR_DW32 branch). Defaults to 16-bit for any variant that
	 * leaves bus_width=16 (the existing T41 SKUs).
	 */
	{
		u32 dwcfg_buswidth = (v->bus_width == 32) ? 1u : 0u;

		ddr_writel(p, DDRC_DWCFG_DFI_INIT_START | dwcfg_buswidth,
			   DDRC_DWCFG);
		ddr_writel(p, dwcfg_buswidth, DDRC_DWCFG);
		while (!(ddr_readl(p, DDRC_DWSTATUS) & DDRC_DWSTATUS_DFI_INIT_COMP))
			;
	}

#ifdef CONFIG_SOC_A1
	/* A1 vendor settles 50 us after dfi_init_complete before deasserting
	 * dfi_reset_n; T40/T41 go straight on. */
	if (v->family == INGENIC_DDR_FAMILY_A1)
		udelay(50);
#endif

	ddr_writel(p, 0, DDRC_CTRL);			/* dfi_reset_n high */
	udelay(5);
	ddr_writel(p, v->ddrc_cfg, DDRC_CFG);
	/*
	 * Vendor T40 ddrc_dfi_init: 500 us settle AFTER CFG write, BEFORE
	 * raising CKE - DRAM needs time to see configuration latched before
	 * CKE + commands are valid. Skipping made dram_verify intermittently
	 * fail. T41 used a shorter 5 us delay so leave that branch. A1 also
	 * uses the 500 us settle.
	 */
	if (v->family == INGENIC_DDR_FAMILY_T40
#ifdef CONFIG_SOC_A1
	    || v->family == INGENIC_DDR_FAMILY_A1
#endif
	   )
		udelay(500);
	else
		udelay(5);
	ddr_writel(p, DDRC_CTRL_CKE, DDRC_CTRL);	/* CKE high */
	udelay(10);

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

		if (v->family == INGENIC_DDR_FAMILY_T40) {
			/* Vendor T40 DDR2 sequence: 5ms MR0 settle, PCHG, AUREF
			 * x2, then another 5ms before HW calibration runs. */
			udelay(5);
			udelay(5 * 1000);
			ddr_writel(p, 0x400003, DDRC_LMR);
			udelay(100);
			ddr_writel(p, 0x43, DDRC_LMR);
			udelay(5);
			ddr_writel(p, 0x43, DDRC_LMR);
			udelay(5 * 1000);
		} else {
			/* T41 vendor sequence: shorter delays + four trailing
			 * extended-LMR words (likely ZQ/EMR sub-modes). */
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
		}
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
		/*
		 * A1 pre-bakes the kgd ODT/DS patch into mr1 at transcription
		 * (vendor patches MR1 bits A9/A6/A2 + A5/A1 from kgd_odt/kgd_ds);
		 * emit it verbatim. T40/T41 patch RTT_DIC at runtime instead.
		 */
#ifdef CONFIG_SOC_A1
		if (v->family == INGENIC_DDR_FAMILY_A1)
			ddr_writel(p, MR_DDR3(v->mr1), DDRC_LMR);
		else
#endif
			ddr_writel(p, (MR_DDR3(v->mr1) & ~0x266000) |
				   (kgd_rtt_dic << 12), DDRC_LMR);
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

	/*
	 * A1 has a shifted CPM map (DDRCDR at 0x3c, not 0x2c) and ungates
	 * the DDR clock itself, so it cannot share the divider write below;
	 * it uses its own CGU path in ddr_innophy_phy_a1.c.
	 */
#ifdef CONFIG_SOC_A1
	if (v->family == INGENIC_DDR_FAMILY_A1) {
		ingenic_ddr_a1_cgu_init(v);
		return;
	}
#endif

	/*
	 * Source-mux select: vendor cgu_clk_sel[DDR] = MPLL (bits 31:30 =
	 * 0b10) on the whole XBurst2 family. The earlier assumption that T40
	 * "leaves the source alone" was wrong: a reliable vendor T40N U-Boot
	 * reads back DDRCDR = 0xa0000001 (source 2 = MPLL), while the bootrom
	 * leaves source = 1 (a different mux). Skipping this write on T40 left
	 * the DDR clocked from the wrong source - the cause of the intermittent
	 * first-DRAM-write stall. Always program source = MPLL.
	 */
	r = readl(ddrcdr);
	r &= ~(3u << 30);
	r |= (2u << 30);			/* source = MPLL */
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
	const struct ingenic_ddr_variant *v = p->cfg;
	bool is_t40 = (v->family == INGENIC_DDR_FAMILY_T40);
	int ret;

	debug("ingenic-ddr: init %s (%s, %u MHz, family %d)\n",
	      v->name, v->chip, v->ddr_hz / 1000000, v->family);

	ingenic_ddr_cgu_init(v);

	ddrc_reset_phy(p);

	/*
	 * Family-specific PHY init. The T40 path also programs drive/ODT
	 * via hard-coded T40N values (vendor t40n_phy_driver_odt is folded
	 * into ingenic_ddr_t40_phy_init); the T41 path uses the efuse
	 * par[] table programmed by ingenic_ddr_phy_set_drv_odt() below.
	 * The A1 path programs drive/ODT/DQS/DQ/VREF from its a1_phy table
	 * inside ingenic_ddr_a1_phy_init() (A1 builds only).
	 */
	if (is_t40)
		ret = ingenic_ddr_t40_phy_init(p);
#ifdef CONFIG_SOC_A1
	else if (v->family == INGENIC_DDR_FAMILY_A1)
		ret = ingenic_ddr_a1_phy_init(p);
#endif
	else
		ret = ingenic_ddr_phy_init(p);
	if (ret)
		return ret;

	ddrc_dfi_init(p);

	/* T41 programs drive/ODT here; T40 folds it into phy_init, A1 into
	 * a1_phy_init. */
	if (!is_t40
#ifdef CONFIG_SOC_A1
	    && v->family != INGENIC_DDR_FAMILY_A1
#endif
	   )
		ingenic_ddr_phy_set_drv_odt(p);

	ddrc_prev_init(p);

	if (is_t40)
		ret = ingenic_ddr_t40_phy_hw_calibration(p);
#ifdef CONFIG_SOC_A1
	else if (v->family == INGENIC_DDR_FAMILY_A1)
		ret = ingenic_ddr_a1_phy_hw_calibration(p);
#endif
	else
		ret = ingenic_ddr_phy_hw_calibration(p);
	if (ret)
		return ret;

	ddrc_post_init(p);
	ddrc_autosr_setup(p);

	if (is_t40) {
		ingenic_ddr_t40_post_phy_fixups(p);
		ingenic_ddr_t40_phy_set_skew(p);
#ifdef CONFIG_SOC_A1
	} else if (v->family == INGENIC_DDR_FAMILY_A1) {
		/* A1 vendor tail: rfifo (MEM_CFG bit5 + RFIFO[2:0]=3). */
		ingenic_ddr_a1_post_phy_fixups(p);
#endif
	} else {
		/* Optimize DDR bandwidth (T41 vendor magic writes at end). */
		writel(0x0,         (void __iomem *)(0xb301206cu));
		writel(0xff000000,  (void __iomem *)(0xb3012040u));
		writel(0x2bd07460,  (void __iomem *)(0xb3012048u));
		writel(0x1,         (void __iomem *)(0xb301206cu));

		ingenic_ddr_phy_set_vref_skew(p);
	}

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

/* Map ingenic,variant DT string to the per-variant config table. */
static const struct {
	const char *name;
	const struct ingenic_ddr_variant *cfg;
} ingenic_ddr_variants[] = {
	{ "t41a",   &ingenic_ddr_variant_t41a   },
	{ "t41l",   &ingenic_ddr_variant_t41l   },
	{ "t41lq",  &ingenic_ddr_variant_t41lq  },
	{ "t41n",   &ingenic_ddr_variant_t41n   },
	{ "t41nq",  &ingenic_ddr_variant_t41nq  },
	{ "t41xq",  &ingenic_ddr_variant_t41xq  },
	{ "t41zg",  &ingenic_ddr_variant_t41zg  },
	{ "t41zgc", &ingenic_ddr_variant_t41zgc },
	{ "t41zl",  &ingenic_ddr_variant_t41zl  },
	{ "t41zm",  &ingenic_ddr_variant_t41zm  },
	{ "t41zmc", &ingenic_ddr_variant_t41zmc },
	{ "t41zn",  &ingenic_ddr_variant_t41zn  },
	{ "t41zx",  &ingenic_ddr_variant_t41zx  },
	/* T40 family */
	{ "t40a",   &ingenic_ddr_variant_t40a   },
	{ "t40n",   &ingenic_ddr_variant_t40n   },
	{ "t40nn",  &ingenic_ddr_variant_t40n   },	/* alias - same silicon */
	{ "t40xp",  &ingenic_ddr_variant_t40xp  },
#ifdef CONFIG_SOC_A1
	/* A1 family */
	{ "a1n",    &ingenic_ddr_variant_a1n    },
	{ "a1nt",   &ingenic_ddr_variant_a1nt   },
	{ "a1x",    &ingenic_ddr_variant_a1x    },
	{ "a1a",    &ingenic_ddr_variant_a1x    },	/* A1A reuses the A1X profile (no A1A HW yet) */
	{ "a1l",    &ingenic_ddr_variant_a1l    },
#endif
};

/*
 * SPL helper for the per-SoC pll.c: read ingenic,variant from the DT
 * node matching `compatible` and return that SKU's PLL setpoints. Runs
 * before driver model, so it reads the FDT blob directly (the caller
 * must have set gd->fdt_blob, e.g. via fdtdec_setup()).
 */
int ingenic_ddr_pll_setpoints(const char *compatible, u32 *apll_mnod,
			      u32 *mpll_mnod, u32 *vpll_mnod)
{
	const void *blob = gd->fdt_blob;
	const char *name;
	int node, i;

	if (!blob)
		return -ENODEV;
	node = fdt_node_offset_by_compatible(blob, -1, compatible);
	if (node < 0)
		return -ENODEV;
	name = fdt_getprop(blob, node, "ingenic,variant", NULL);
	if (!name)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(ingenic_ddr_variants); i++) {
		if (!strcmp(name, ingenic_ddr_variants[i].name)) {
			*apll_mnod = ingenic_ddr_variants[i].cfg->apll_mnod;
			*mpll_mnod = ingenic_ddr_variants[i].cfg->mpll_mnod;
			*vpll_mnod = ingenic_ddr_variants[i].cfg->vpll_mnod;
			return 0;
		}
	}
	return -ENODEV;
}

static int ingenic_ddr_probe(struct udevice *dev)
{
	struct ingenic_ddr_priv *p = dev_get_priv(dev);
	const struct ingenic_ddr_variant *v = NULL;
	const char *variant_name;
	fdt_addr_t base;
	u64 size;
	int i;

	variant_name = dev_read_string(dev, "ingenic,variant");
	if (!variant_name) {
		dev_err(dev, "missing ingenic,variant property\n");
		return -EINVAL;
	}
	for (i = 0; i < ARRAY_SIZE(ingenic_ddr_variants); i++) {
		if (!strcmp(variant_name, ingenic_ddr_variants[i].name)) {
			v = ingenic_ddr_variants[i].cfg;
			break;
		}
	}
	if (!v) {
		dev_err(dev, "unknown ingenic,variant '%s'\n", variant_name);
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
	/* The same UCLASS_RAM driver covers every XBurst2 SoC that uses
	 * this IP; the per-SoC compatible exists for binding clarity at
	 * the DT level (so each board's DT names the SoC explicitly).
	 * The variant struct is selected by the `ingenic,variant` DT
	 * property, not the compatible. */
	{ .compatible = "ingenic,a1-ddr-innophy" },
	{ .compatible = "ingenic,t40-ddr-innophy" },
	{ .compatible = "ingenic,t41-ddr-innophy" },
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
