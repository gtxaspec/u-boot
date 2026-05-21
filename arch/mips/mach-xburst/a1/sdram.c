// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 DDR3 Innophy init (SPL)
 *
 * Faithful transliteration of the vendor ddr_innophy.c sdram_init()
 * path for A1N (DDR3-800, 256 MB, 16-bit datawidth). The vendor
 * ddr_innophy.c is 3727 lines; this is the minimal subset needed
 * to bring up DDR on A1N with hardware calibration.
 *
 * Register values come from the vendor ddr_params_creator output
 * (ddr_reg_values_a1n.c) and the per-variant ddr3_param_t tuning
 * (a1n_ddr3_param in ddr_innophy.c).
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <mach/a1.h>

/* DDRC register offsets from DDRC_BASE (0xb34f0000) */
#define DDRC_STATUS	0x00
#define DDRC_CFG	0x08
#define DDRC_CTRL	0x10
#define DDRC_LMR	0x18
#define DDRC_AUTOSR_EN	0x28
#define DDRC_AUTOSR_CNT	0x30
#define DDRC_REFCNT	0x38
#define DDRC_TIM(n)	(0x40 + 8 * ((n) - 1))
#define DDRC_MMAP0	0x78
#define DDRC_MMAP1	0x80
#define DDRC_HREGPRO	0xd8

/* DDRC APB registers at DDRC_BASE + DDR_PHY_OFFSET + 0x1000 */
#define DDRC_APB_BASE	0xb3012000
#define DDRC_DWCFG	0x00
#define DDRC_DWSTATUS	0x04
#define DDRC_REMAP(n)	(0x08 + 4 * ((n) - 1))
#define DDRC_CGUC0	0x64
#define DDRC_CGUC1	0x68
#define DDRC_PREGPRO	0x6c

/* Innophy PHY register offsets from DDR_PHY_BASE (0xb3011000) */
#define PHY_RST		0x000
#define PHY_MEM_CFG	0x004
#define PHY_TRAIN_CTRL	0x008
#define PHY_CL		0x014
#define PHY_AL		0x018
#define PHY_CWL	0x01c
#define PHY_DQ_WIDTH	0x034
#define PHY_PLL_FBDIV	0x140
#define PHY_PLL_CTRL	0x144
#define PHY_PLL_PDIV	0x148
#define PHY_PLL_PD	0x14c
#define PHY_PLL_LOCK	0x180
#define PHY_CALIB_DONE	0x184

static void ddr_writel(u32 val, u32 off)
{
	writel(val, (void __iomem *)(DDRC_BASE + off));
}

static u32 ddr_readl(u32 off)
{
	return readl((void __iomem *)(DDRC_BASE + off));
}

static void apb_writel(u32 val, u32 off)
{
	writel(val, (void __iomem *)(DDRC_APB_BASE + off));
}

static u32 apb_readl(u32 off)
{
	return readl((void __iomem *)(DDRC_APB_BASE + off));
}

static void phy_writel(u32 val, u32 off)
{
	writel(val, (void __iomem *)(DDR_PHY_BASE + off));
}

static u32 phy_readl(u32 off)
{
	return readl((void __iomem *)(DDR_PHY_BASE + off));
}

static void phy_writel_idx(u32 val, u32 idx)
{
	writel(val, (void __iomem *)(DDR_PHY_BASE + idx * 4));
}

static void ddr_clk_set(void)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 val;

	/* Ungate DDR clock (CLKGR0 bit 3) */
	val = readl(cpm + CPM_CLKGR0);
	val &= ~CPM_CLKGR0_DDR;
	writel(val, cpm + CPM_CLKGR0);

	/*
	 * DDR clock rate is set by pll_init (MPLL=1608) + bootrom's DDRCDR
	 * config (MPLL/2 = 804 MHz). No DRCG toggle needed on A1 - the
	 * vendor does not touch DRCG in the SPL DDR init path.
	 */
}

static void ddrc_reset_phy(void)
{
	ddr_writel(0x00f00000, DDRC_CTRL);
	{ volatile int d = 100000; while (d--); }
	ddr_writel(0x00800000, DDRC_CTRL);
	{ volatile int d = 100000; while (d--); }
}

static void ddrp_pll_init(void)
{
	phy_writel(0x0a, PHY_MEM_CFG);
	phy_writel(0x03, PHY_DQ_WIDTH);
	phy_writel(0x0d, PHY_RST);
	phy_writel(0x08, PHY_CWL);
	phy_writel(0x0b, PHY_CL);
	phy_writel(0x00, PHY_AL);
	phy_writel(0x00, PHY_PLL_PD);

	u32 ctrl = phy_readl(PHY_PLL_CTRL);
	phy_writel(ctrl | 0x80, PHY_PLL_CTRL);

	u32 pd = phy_readl(PHY_PLL_PD);
	phy_writel(pd | 0x20, PHY_PLL_PD);

	while (!(phy_readl(PHY_PLL_LOCK) & 0x4))
		;
}

static void ddr_param_write(void)
{
	/* ODT pull-down/up for all 4 byte lanes */
	static const u32 odt_pd_idx[] = { 0x140, 0x150, 0x160, 0x170 };
	static const u32 odt_pu_idx[] = { 0x141, 0x151, 0x161, 0x171 };
	int i;

	for (i = 0; i < 4; i++) {
		phy_writel_idx(1, odt_pd_idx[i]);
		phy_writel_idx(0, odt_pu_idx[i]);
	}

	/* CMD drive strength */
	phy_writel_idx(0x0e, 0x130);
	phy_writel_idx(0x0e, 0x131);
	/* CK drive strength */
	phy_writel_idx(0x0e, 0x132);
	phy_writel_idx(0x0e, 0x133);

	/* DQ drive A lanes (pull-down/up) */
	static const u32 drv_a_pd[] = { 0x142, 0x152 };
	static const u32 drv_a_pu[] = { 0x143, 0x153 };
	for (i = 0; i < 2; i++) {
		phy_writel_idx(0x14, drv_a_pd[i]);
		phy_writel_idx(0x14, drv_a_pu[i]);
	}
	/* DQ drive B lanes */
	static const u32 drv_b_pd[] = { 0x162, 0x172 };
	static const u32 drv_b_pu[] = { 0x163, 0x173 };
	for (i = 0; i < 2; i++) {
		phy_writel_idx(0x14, drv_b_pd[i]);
		phy_writel_idx(0x14, drv_b_pu[i]);
	}

	/* DQS A delay */
	static const u32 dqs_a[] = { 0x1d2, 0x1e7, 0x1ea, 0x1eb };
	for (i = 0; i < 4; i++)
		phy_writel_idx(0x20, dqs_a[i]);
	/* DQS B delay */
	static const u32 dqs_b[] = { 0x232, 0x247, 0x24a, 0x24b };
	for (i = 0; i < 4; i++)
		phy_writel_idx(0x20, dqs_b[i]);

	/* Per-DQ-bit delay offsets from vendor DQxRxOFFSET table:
	 * DQ0-7: +0x02,+0x04,...,+0x10 (even, low byte)
	 * DQ8-15: +0x17,+0x19,...,+0x25 (odd, high byte) */
	static const u32 dq_off[16] = {
		0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10,
		0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x23, 0x25,
	};
	/* Channel A (base 0x1c0) */
	for (i = 0; i < 16; i++)
		phy_writel_idx(0x10, 0x1c0 + dq_off[i]);
	/* Channel B (base 0x220) */
	for (i = 0; i < 16; i++)
		phy_writel_idx(0x07, 0x220 + dq_off[i]);

	/* VREF for all 4 byte lanes */
	static const u32 vref_idx[] = { 0x147, 0x157, 0x167, 0x177 };
	for (i = 0; i < 4; i++)
		phy_writel_idx(0x80, vref_idx[i]);
}

extern void a1_spl_putc(char c);

static void ddrc_dfi_init(void)
{
	int timeout;

	/* DFI init handshake (16-bit bus) */
	apb_writel(0x08, DDRC_DWCFG);
	apb_writel(0x00, DDRC_DWCFG);

	timeout = 1000000;
	while (!(apb_readl(DDRC_DWSTATUS) & 0x1) && --timeout)
		;
	if (!timeout)
		a1_spl_putc('!');
	else
		a1_spl_putc('.');
	{ volatile int _d = 5000; while (_d--); }

	ddr_writel(0x00000000, DDRC_CTRL);
	ddr_writel(0x4a004a35, DDRC_CFG);
	{ volatile int _d = 50000; while (_d--); }
	ddr_writel(0x00000002, DDRC_CTRL);
	{ volatile int _d = 1000; while (_d--); }

	/* MR2 */
	ddr_writel(0x00000000, DDRC_LMR);
	{ volatile int _d = 500; while (_d--); }
	ddr_writel(0x00018483, DDRC_LMR);
	{ volatile int _d = 500; while (_d--); }
	/* MR3 */
	ddr_writel(0x00000000, DDRC_LMR);
	{ volatile int _d = 500; while (_d--); }
	ddr_writel(0x00000683, DDRC_LMR);
	{ volatile int _d = 500; while (_d--); }
	/* MR1 (patched with kgd_odt=1, kgd_ds=1) */
	ddr_writel(0x00000000, DDRC_LMR);
	{ volatile int _d = 500; while (_d--); }
	ddr_writel(0x00006283, DDRC_LMR);
	{ volatile int _d = 500; while (_d--); }
	/* MR0 */
	ddr_writel(0x00000000, DDRC_LMR);
	{ volatile int _d = 500; while (_d--); }
	ddr_writel(0x01f70083, DDRC_LMR);
	{ volatile int _d = 500; while (_d--); }
	/* ZQCL CS0 */
	ddr_writel(0x000000c3, DDRC_LMR);
	{ volatile int _d = 500; while (_d--); }
}

static void ddrc_prev_init(void)
{
	ddr_writel(0x07130d08, DDRC_TIM(1));
	ddr_writel(0x0809070b, DDRC_TIM(2));
	ddr_writel(0x030c040c, DDRC_TIM(3));
	ddr_writel(0x252b1f07, DDRC_TIM(4));
	ddr_writel(0x80069055, DDRC_TIM(5));
	ddr_writel(0x000020f0, DDRC_MMAP0);
	ddr_writel(0x00003000, DDRC_MMAP1);
	ddr_writel(0x00008092, DDRC_CTRL);
}

static void ddrp_hardware_calibration(void)
{
	phy_writel(0x00, PHY_TRAIN_CTRL);
	phy_readl(PHY_TRAIN_CTRL);
	phy_writel(0x01, PHY_TRAIN_CTRL);

	while ((phy_readl(PHY_CALIB_DONE) & 0xf) != 0x3)
		;

	u32 ctrl = phy_readl(PHY_TRAIN_CTRL);
	phy_writel(ctrl & ~0x01, PHY_TRAIN_CTRL);
}

static void ddrc_post_init(void)
{
	ddr_writel(0x41c30081, DDRC_REFCNT);

	/* Address remap (vendor A1N remap array) */
	apb_writel(0x030f0e0d, DDRC_REMAP(1));
	apb_writel(0x07060504, DDRC_REMAP(2));
	apb_writel(0x0b0a0908, DDRC_REMAP(3));
	apb_writel(0x0201000c, DDRC_REMAP(4));
	apb_writel(0x13121110, DDRC_REMAP(5));

	ddr_writel(0x0000b092, DDRC_CTRL);
	apb_writel(0x11111111, DDRC_CGUC0);
	apb_writel(0x00000113, DDRC_CGUC1);
}

static void ddrp_set_rfifo(void)
{
	u32 val;

	val = phy_readl(PHY_MEM_CFG);
	phy_writel(val | (1u << 5), PHY_MEM_CFG);

	val = phy_readl(DDR_PHY_OFFSET + 0x038);
	val &= ~0x7;
	val |= 0x3;
	phy_writel(val, DDR_PHY_OFFSET + 0x038);
}

extern void a1_spl_puts(const char *s);

void sdram_init(void)
{
	a1_spl_puts("DDR: clk\n");
	ddr_clk_set();
	a1_spl_puts("DDR: clk ok\n");

	a1_spl_puts("DDR: rst\n");
	ddrc_reset_phy();

	a1_spl_puts("DDR: pll\n");
	ddrp_pll_init();

	a1_spl_puts("DDR: phy\n");
	ddr_param_write();

	a1_spl_puts("DDR: dfi\n");
	ddrc_dfi_init();

	a1_spl_puts("DDR: prev\n");
	ddrc_prev_init();

	a1_spl_puts("DDR: cal\n");
	ddrp_hardware_calibration();

	a1_spl_puts("DDR: post\n");
	ddrc_post_init();

	ddr_writel(0x40000c41, DDRC_AUTOSR_CNT);
	ddr_writel(0x00000000, DDRC_AUTOSR_EN);
	ddr_writel(0x00000001, DDRC_HREGPRO);
	apb_writel(0x00000001, DDRC_PREGPRO);

	ddrp_set_rfifo();
	a1_spl_puts("DDR: done\n");
}
