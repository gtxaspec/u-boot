// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T20 (XBurst1) DDR2 controller + Synopsys DWC PHY init.
 *
 * UCLASS_RAM driver. T20 is the only XBurst1 camera SoC that is NOT
 * Innophy: it uses the Synopsys DesignWare (DWC) DDR PHY with hardware ZQ
 * impedance calibration and a hardware DQS training engine, so it has its
 * own driver rather than reusing ddr_t31.c. Faithful transliteration of
 * the vendor known-good DWC DDR2 path (arch/mips/cpu/xburst/ddr_dwc.c +
 * t20/ddr_set_dll.c + the DDR branch of t20/clk.c), formerly
 * arch/mips/mach-xburst/t20/sdram.c. The per-SKU register/clock values now
 * come from the &ddr node's "ingenic,sdram-params" devicetree array
 * (struct ingenic_t20_ddr_params) instead of compile-time
 * CONFIG_T20_VARIANT_* / #if branches - the mainline rk3328 DMC idiom.
 *
 * Probes off DT in both SPL (DDR bring-up) and U-Boot proper (just records
 * DRAM size for ram_get_info()). The register write order, PIR/PGSR poll
 * loops and delays are timing-critical and reproduced exactly from the
 * vendor source; bypass is hard 0 (DDR 500 MHz) and the DRAM is always
 * DDR2, so the LPDDR/DDR3/bypass branches are dropped.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#define LOG_CATEGORY UCLASS_RAM

#include <dm.h>
#include <dt-structs.h>
#include <log.h>
#include <ram.h>
#include <fdtdec.h>
#include <dm/device_compat.h>
#include <linux/delay.h>
#include <linux/libfdt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/global_data.h>
#include <mach/t20.h>
#include "ddr_t20.h"

DECLARE_GLOBAL_DATA_PTR;

#define ddr_writel(v, reg)	writel((v), (void __iomem *)(DDRC_BASE + (reg)))
#define ddr_readl(reg)		readl((void __iomem *)(DDRC_BASE + (reg)))
#define phy_writel(v, reg)	writel((v), (void __iomem *)(DDR_PHY_BASE + (reg)))
#define phy_readl(reg)		readl((void __iomem *)(DDR_PHY_BASE + (reg)))

/* SPL raw console (mach-xburst/t20/serial.c); SPL-only, see ddr_die(). */
void t20_spl_puts(const char *s);

static const u32 out_imp_table[] = DDRP_IMPANDCE_ARRAY;
static const u32 odt_imp_table[] = DDRP_ODT_IMPANDCE_ARRAY;
static const u8  rzq_table[]     = DDRP_RZQ_TABLE;

static u32 cpm_readl(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void cpm_writel(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

static void ddr_die(const char *what)
{
#ifdef CONFIG_XPL_BUILD
	t20_spl_puts("T20 SPL: DDR FAIL ");
	t20_spl_puts(what);
	t20_spl_puts("\n");
#else
	(void)what;
#endif
	for (;;)
		;
}

/*
 * DDR clock divider: the DDR branch of the vendor clk_set_rate(). Source
 * MPLL (1000 MHz), target 500 MHz, so cdr = 1. Leave the PLL-select bits
 * [31:30] as the mask ROM set them (DDR already runs off a PLL to have
 * reached here); only clear the divider field and set CE (bit 29) + cdr,
 * then poll BUSY (bit 28). Same on every T20 SKU (all DDR 500).
 */
static void ddr_clk_set_rate(void)
{
	unsigned int cdr = ((DDR_MPLL_RATE + DDR_TARGET_RATE - 1) /
			    DDR_TARGET_RATE - 1) & 0xff;
	u32 regval = cpm_readl(CPM_DDRCDR);

	regval &= ~(0xf | (0x3f << 24));
	regval |= ((1 << 29) | cdr);		/* ce = bit 29 */
	cpm_writel(regval, CPM_DDRCDR);
	while (cpm_readl(CPM_DDRCDR) & (1 << 28))	/* busy = bit 28 */
		;
}

/*
 * prev_ddr_init hook from t20/ddr_set_dll.c (reset_dllA).
 * WARNING (vendor): CPM_DRCG (0xb00000d0) BIT6 must stay set (0x40);
 * clearing it makes chip memory unstable and the GPU hangs.
 */
static void reset_dllA(void)
{
	cpm_writel(0x73 | (1 << 6), CPM_DRCG);
	mdelay(1);
	cpm_writel(0x71 | (1 << 6), CPM_DRCG);
	mdelay(1);
}

static void wait_ddrp_pgsr(unsigned int wait_val, int timeout, const char *what)
{
	while (((phy_readl(DDRP_PGSR) & wait_val) != wait_val) && --timeout)
		;
	if (timeout == 0)
		ddr_die(what);
}

static void mem_remap(const struct ingenic_t20_ddr_params *cfg)
{
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(cfg->remap); i++)
		ddr_writel(cfg->remap[i], DDRC_REMAP(i + 1));
}

static void ddr_controller_init(const struct ingenic_t20_ddr_params *cfg)
{
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
	ddr_writel(DDRC_REFCNT_VALUE, DDRC_REFCNT);
	ddr_writel(DDRC_CTRL_VALUE, DDRC_CTRL);

	mem_remap(cfg);

	ddr_writel(ddr_readl(DDRC_STATUS) & ~DDRC_DSTATUS_MISS, DDRC_STATUS);
	/* DDRC_AUTOSR_EN_VALUE == 0: the autosr/DLP branch is a no-op. */
	ddr_writel(DDRC_AUTOSR_EN_VALUE, DDRC_AUTOSR_EN);
}

/* DDR2 path of the vendor ddr_phy_param_config(). */
static void ddr_phy_param_config(const struct ingenic_t20_ddr_params *cfg)
{
	phy_writel(cfg->ddrp_dcr, DDRP_DCR);
	/* vendor leaves ODTCR commented out */
	phy_writel(DDRP_PTR0_VALUE, DDRP_PTR0);
	phy_writel(DDRP_PTR1_VALUE, DDRP_PTR1);
	phy_writel(DDRP_PTR2_VALUE, DDRP_PTR2);
	phy_writel(cfg->ddrp_dtpr0, DDRP_DTPR0);
	phy_writel(cfg->ddrp_dtpr1, DDRP_DTPR1);
	phy_writel(DDRP_DTPR2_VALUE, DDRP_DTPR2);

	phy_writel(DDRP_DX0GCR_VALUE, DDRP_DXGCR(0));
	phy_writel(DDRP_DX1GCR_VALUE, DDRP_DXGCR(1));
	phy_writel(DDRP_DX2GCR_VALUE, DDRP_DXGCR(2));
	phy_writel(DDRP_DX3GCR_VALUE, DDRP_DXGCR(3));

	phy_writel(DDRP_PGCR_VALUE, DDRP_PGCR);

	/* DDR2: DQS pull config, MR0, MR1 */
	phy_writel(0x910, DDRP_DXCCR);
	phy_writel(DDRP_MR0_VALUE, DDRP_MR0);
	phy_writel(DDRP_MR1_VALUE, DDRP_MR1);
}

static void ddr_phy_impedance_calibration(unsigned int cal_value)
{
	unsigned int pull[4];
	unsigned int val = cal_value;
	int i, j;

	for (i = 0; i < 4; i++) {
		pull[i] = (val >> (5 * i)) & 0x1f;
		for (j = 0; j < (int)ARRAY_SIZE(rzq_table); j++)
			if (pull[i] == rzq_table[j])
				break;
		pull[i] = j;
	}

	pull[0] = (out_imp_table[0] * pull[0] + out_imp_table[1] / 2) /
		  out_imp_table[1];
	pull[1] = (out_imp_table[0] * pull[1] + out_imp_table[1] / 2) /
		  out_imp_table[1];
	pull[2] = (odt_imp_table[0] * pull[2] + odt_imp_table[1] / 2) /
		  odt_imp_table[1];
	pull[3] = (odt_imp_table[0] * pull[3] + odt_imp_table[1] / 2) /
		  odt_imp_table[1];

	val = phy_readl(DDRP_ZQXCR0(0));
	val &= 0x10000000;
	for (i = 0; i < 4; i++)
		val |= (rzq_table[pull[i]] << (5 * i));
	val |= DDRP_ZQXCR_ZDEN;
	phy_writel(val, DDRP_ZQXCR0(0));
}

/* DDR2, non-bypass path of the vendor ddr_phy_init_dram(). */
static void ddr_phy_init_dram(void)
{
	unsigned int pir_val;
	unsigned int wait_val;
	unsigned int val;

	pir_val = DDRP_PIR_INIT | DDRP_PIR_DLLSRST | DDRP_PIR_ITMSRST |
		  DDRP_PIR_DRAMINT | DDRP_PIR_DLLLOCK | DDRP_PIR_ZCAL;
	wait_val = DDRP_PGSR_IDONE | DDRP_PGSR_ZCDONE | DDRP_PGSR_DIDONE |
		   DDRP_PGSR_DLDONE;

	/* ZCAL is set: prime the ZQ impedance control register. */
	val = phy_readl(DDRP_ZQXCR0(0));
	val &= ~((1 << 31) | (1 << 29) | (1 << 28) | 0xfffff);
	val |= (1 << 30);
	phy_writel(val, DDRP_ZQXCR0(0));
	phy_writel(DDRP_ZQNCR1_VALUE, DDRP_ZQXCR1(0));

	wait_ddrp_pgsr(DDRP_PGSR_IDONE | DDRP_PGSR_DLDONE | DDRP_PGSR_ZCDONE,
		       0x10000, "phy idle");
	phy_writel(pir_val, DDRP_PIR);
	wait_ddrp_pgsr(wait_val, 10000, "dram init");

	val = phy_readl(DDRP_ZQXSR0(0));
	if (val & 0x40000000)
		ddr_die("zq calib");
	ddr_phy_impedance_calibration(val);
}

/* non-bypass path of the vendor ddr_training_hardware(). */
static void ddr_training_hardware(void)
{
	unsigned int pir_val = DDRP_PIR_INIT | DDRP_PIR_QSTRN;
	unsigned int wait_val = DDRP_PGSR_IDONE | DDRP_PGSR_DTDONE;
	int result;

	phy_writel(pir_val, DDRP_PIR);
	wait_ddrp_pgsr(wait_val, 500000, "dqs train");
	result = phy_readl(DDRP_PGSR);
	if (result & (DDRP_PGSR_DTERR | DDRP_PGSR_DTIERR))
		ddr_die("dqs train err");
}

static void ddr_phy_init(const struct ingenic_t20_ddr_params *cfg)
{
	phy_writel(0x150000, DDRP_DTAR);	/* training address */
	ddr_phy_param_config(cfg);
	ddr_phy_init_dram();
	ddr_training_hardware();
}

static void controller_reset_phy(const struct ingenic_t20_ddr_params *cfg)
{
	ddr_writel(0xf << 20, DDRC_CTRL);
	mdelay(1);
	ddr_writel(0, DDRC_CTRL);
	mdelay(1);
	/* force CKE1/CS1 + CS0 high */
	ddr_writel(cfg->ddrc_cfg | DDRC_CFG_CS1EN | DDRC_CFG_CS0EN, DDRC_CFG);
	ddr_writel((1 << 1), DDRC_CTRL);
}

/*
 * DT compatible for the T20 DWC DDR node - one binding for every T20 SKU
 * (the board DT picks the part via "ingenic,sdram-params"). Shared by the
 * pre-DM fdt parse below and the driver of_match.
 */
#define INGENIC_T20_DDR_COMPATIBLE	"ingenic,t20-ddr-dwc"

/*
 * Deserialize the &ddr node's "ingenic,sdram-params" u32 array straight out
 * of @blob into @out. Gd-free / libfdt-only so it can run in the pre-DM
 * board_init_f bring-up (before DDR, before driver model). The struct is
 * all-u32 in the array's order, so this is a flat fdt32->cpu copy (the
 * rk3328 (u32 *)&params idiom). Returns 0 on success, negative on error.
 */
static int ingenic_t20_ddr_get_params(const void *blob,
				      struct ingenic_t20_ddr_params *out)
{
	const fdt32_t *arr;
	u32 *w = (u32 *)out;
	int node, len, i;

	if (!blob)
		return -ENODEV;

	node = fdt_node_offset_by_compatible(blob, -1,
					     INGENIC_T20_DDR_COMPATIBLE);
	if (node < 0)
		return -ENODEV;

	arr = fdt_getprop(blob, node, "ingenic,sdram-params", &len);
	if (!arr || len != (int)sizeof(*out))
		return -EINVAL;

	for (i = 0; i < (int)(sizeof(*out) / sizeof(u32)); i++)
		w[i] = fdt32_to_cpu(arr[i]);

	return 0;
}

/*
 * Top-level DDR2 init (the DWC path of the vendor sdram_init()). Called ONCE,
 * imperatively, from board_init_f BEFORE driver model - and critically before
 * the (far, standard-map) BSS exists, since T20's DWC controller hangs on any
 * pre-DDR DRAM access. The params are parsed onto the (cache-as-RAM) stack
 * here, NOT into a BSS static (BSS is far DRAM that isn't up yet); the
 * UCLASS_RAM probe does NOT re-run this, it only records the size.
 */
int ingenic_t20_ddr_sdram_init(const void *blob)
{
	struct ingenic_t20_ddr_params cfg;

	if (ingenic_t20_ddr_get_params(blob, &cfg))
		ddr_die("no sdram-params");

	ddr_clk_set_rate();
	reset_dllA();			/* prev_ddr_init hook */
	controller_reset_phy(&cfg);
	ddr_phy_init(&cfg);
	ddr_controller_init(&cfg);

	/*
	 * Vendor T20 soc.c does a dummy DRAM write immediately after
	 * sdram_init() (settles the DWC controller before first real use);
	 * 0x03fffffc (top of the 64 MB part) is valid on every T20 SKU.
	 */
	*(volatile u32 *)0xa3fffffc = 0x12345678;

	return 0;
}

/*
 * SPL helper for t20/pll.c: the PLL setpoints are array elements [0..2] of
 * the same "ingenic,sdram-params" property. Runs before driver model is up.
 */
int ingenic_t20_ddr_pll_setpoints(const void *blob, u32 *apll_mnod,
				  u32 *mpll_mnod, u32 *cpccr)
{
	struct ingenic_t20_ddr_params cfg;
	int ret;

	ret = ingenic_t20_ddr_get_params(blob, &cfg);
	if (ret)
		return ret;

	*apll_mnod = cfg.apll_mnod;
	*mpll_mnod = cfg.mpll_mnod;
	*cpccr = cfg.cpccr;
	return 0;
}

/* ------------------------------------------------------------------
 * UCLASS_RAM driver. DDR is brought up imperatively in board_init_f (before
 * driver model, so the DM heap lands in real DRAM and to dodge T20's DWC
 * pre-DDR hang); the probe does NOT touch the controller, it only records
 * the DRAM size for ram_get_info(). The per-SKU params come from the &ddr
 * node's "ingenic,sdram-params" array, read into platdata by of_to_plat -
 * the mainline rk3328 DMC shape, so the same driver also works under
 * OF_PLATDATA in a future TPL. Unlike rk3328 (which detects size from a HW
 * register and so only needs platdata in TPL), T20 has no size-detect, so
 * of_to_plat / plat_auto are present in every phase that probes RAM.
 * ------------------------------------------------------------------ */

struct ingenic_t20_ddr_plat {
#if CONFIG_IS_ENABLED(OF_PLATDATA)
	struct dtd_ingenic_t20_ddr_dwc dtplat;
#else
	struct ingenic_t20_ddr_params params;
#endif
};

struct ingenic_t20_ddr_priv {
	u32 ram_size;			/* total bytes, for ram_get_info() */
};

static int ingenic_t20_ddr_of_to_plat(struct udevice *dev)
{
#if CONFIG_IS_ENABLED(OF_REAL)
	struct ingenic_t20_ddr_plat *plat = dev_get_plat(dev);
	int ret;

	ret = dev_read_u32_array(dev, "ingenic,sdram-params",
				 (u32 *)&plat->params,
				 sizeof(plat->params) / sizeof(u32));
	if (ret) {
		dev_err(dev, "Cannot read ingenic,sdram-params %d\n", ret);
		return ret;
	}
#endif
	return 0;
}

static int ingenic_t20_ddr_probe(struct udevice *dev)
{
	struct ingenic_t20_ddr_priv *p = dev_get_priv(dev);
	struct ingenic_t20_ddr_plat *plat = dev_get_plat(dev);
#if CONFIG_IS_ENABLED(OF_PLATDATA)
	const struct ingenic_t20_ddr_params *params =
		(const struct ingenic_t20_ddr_params *)
			plat->dtplat.ingenic_sdram_params;
#else
	const struct ingenic_t20_ddr_params *params = &plat->params;
#endif

	p->ram_size = params->chip0_size;
	return 0;
}

static int ingenic_t20_ddr_get_info(struct udevice *dev, struct ram_info *info)
{
	struct ingenic_t20_ddr_priv *p = dev_get_priv(dev);

	info->base = 0;
	info->size = p->ram_size;
	return 0;
}

static const struct ram_ops ingenic_t20_ddr_ops = {
	.get_info = ingenic_t20_ddr_get_info,
};

static const struct udevice_id ingenic_t20_ddr_ids[] = {
	{ .compatible = INGENIC_T20_DDR_COMPATIBLE },
	{ }
};

U_BOOT_DRIVER(ingenic_t20_ddr) = {
	.name		= "ingenic_t20_ddr",
	.id		= UCLASS_RAM,
	.of_match	= ingenic_t20_ddr_ids,
	.of_to_plat	= ingenic_t20_ddr_of_to_plat,
	.ops		= &ingenic_t20_ddr_ops,
	.probe		= ingenic_t20_ddr_probe,
	.priv_auto	= sizeof(struct ingenic_t20_ddr_priv),
	.plat_auto	= sizeof(struct ingenic_t20_ddr_plat),
};
