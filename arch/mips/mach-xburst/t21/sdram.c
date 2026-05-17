// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T21 DDR2 controller and Innophy PHY init (SPL)
 *
 * Faithful transliteration of the vendor known-good DDR2 path
 * (arch/mips/cpu/xburst/ddr_innophy.c, ddr_set_dll.c, clk.c) for the
 * isvp_t21 (T21N) profile: DDR2 M14D5121632A, 64 MB, 16-bit bus,
 * single CS0, 4-bank, DDR clock 450 MHz (MPLL 900 / 2).
 *
 * Identical Innophy DDR2 init to T31/T23 (same PHY + same 64 MB
 * part as T23); the only T21 delta is the clock target and the
 * 450 MHz timing/MR0 set in mach/t21-ddr.h. The register write
 * order, poll loops and delays are timing-critical and reproduced
 * exactly from the vendor source.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <mach/t21.h>
#include <mach/t21-ddr.h>

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
 * DDR clock divider: replicate the DDR branch of the vendor
 * clk_set_rate(). Source is MPLL (900 MHz), target 450 MHz, so
 * cdr = (pll_rate / rate - 1) & 0xff = 1. From vendor
 * cgu_clk_sel[DDR] = {en, CPM_DDRCDR, sel_bit=30, MPLL, sel[],
 * ce=29, busy=28, stop=27}: change-enable is bit 29, the busy bit
 * to poll is bit 28 (bit 30 is the PLL-select bit and stays set).
 */
static void ddr_clk_set_rate(void)
{
	unsigned int pll_rate = DDR_MPLL_RATE;
	unsigned int rate = DDR_TARGET_RATE;
	unsigned int cdr;
	u32 regval;

	regval = cpm_readl(CPM_DDRCDR);

	if (pll_rate % rate >= rate / 2)
		pll_rate += rate - (pll_rate % rate);
	else
		pll_rate -= (pll_rate % rate);

	cdr = (pll_rate / rate - 1) & 0xff;

	/* DDR path: clear divider (low 4 bits) and the 0x3f<<24 field */
	regval &= ~(0xf | (0x3f << 24));
	regval |= ((1 << 29) | cdr);		/* ce = bit 29 */
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

static void mem_remap(void)
{
	u32 start = 0, num = 0;
	int row, col, dw32, bank8, cs0, cs1;

	row = DDR_ROW;
	col = DDR_COL;
	dw32 = CONFIG_DDR_DW32;
	bank8 = DDR_BANK8;
	cs0 = CONFIG_DDR_CS0;
	cs1 = CONFIG_DDR_CS1;

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

static void ddr_controller_init(void)
{
	ddr_writel(DDRC_CTRL_CKE | DDRC_CTRL_ALH, DDRC_CTRL);
	ddr_writel(0, DDRC_CTRL);

	ddr_writel(DDRC_CFG_VALUE, DDRC_CFG);

	ddr_writel(DDRC_TIMING1_VALUE, DDRC_TIMING(1));
	ddr_writel(DDRC_TIMING2_VALUE, DDRC_TIMING(2));
	ddr_writel(DDRC_TIMING3_VALUE, DDRC_TIMING(3));
	ddr_writel(DDRC_TIMING4_VALUE, DDRC_TIMING(4));
	ddr_writel(DDRC_TIMING5_VALUE, DDRC_TIMING(5));
	ddr_writel(DDRC_TIMING6_VALUE, DDRC_TIMING(6));

	ddr_writel(DDRC_MMAP0_VALUE, DDRC_MMAP0);
	ddr_writel(DDRC_MMAP1_VALUE, DDRC_MMAP1);
	ddr_writel(DDRC_CTRL_CKE | DDRC_CTRL_ALH, DDRC_CTRL);
	ddr_writel(DDRC_REFCNT_VALUE, DDRC_REFCNT);
	ddr_writel(DDRC_CTRL_VALUE & 0xffff8fff, DDRC_CTRL);
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

static void ddr_inno_phy_init(void)
{
	u32 reg = 0;

	phy_writel(0x14, INNO_PLL_FBDIV);
	phy_writel(0x1a, INNO_PLL_CTRL);
	phy_writel(0x5, INNO_PLL_PDIV);
	phy_writel(0x18, INNO_PLL_CTRL);

	while (!(phy_readl(INNO_PLL_LOCK) & (1 << 3)))	/* wait pll lock */
		;

	phy_writel(0x0, INNO_TRAINING_CTRL);
	phy_writel(0x03, INNO_DQ_WIDTH);

	/* MEMSEL = DDR2, BURSEL = burst8 */
	phy_writel(0x11, INNO_MEM_CFG);
	phy_writel(0x0d, INNO_CHANNEL_EN);
	phy_writel(((DDRP_MR0_VALUE & 0xf0) >> 4) - 1, INNO_CWL);
	reg = ((DDRP_MR0_VALUE & 0xf0) >> 4);
	phy_writel(reg, INNO_CL);
	phy_writel(0x00, INNO_AL);

	writel(0, (void __iomem *)DDR_APB_PHY_INIT);	/* start high */
	while (!(readl((void __iomem *)DDR_APB_PHY_INIT) & (1 << 2)))	/* pll locked */
		;
	writel(0, (void __iomem *)REG_DDR_CTRL);

	while (!(readl((void __iomem *)DDR_APB_PHY_INIT) & (1 << 1)))	/* init_complete */
		;
	while (!readl((void __iomem *)T21_INIT_COMP))
		;
	writel(0, (void __iomem *)REG_DDR_CTRL);

	writel(DDRC_CFG_VALUE, (void __iomem *)REG_DDR_CFG);
	writel(0x0a, (void __iomem *)REG_DDR_CTRL);

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

	reg = ((DDRP_MR0_VALUE) << 12) | 0x011;
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

	phy_calibration();

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

/* DDR2 sdram init (innophy DDR2 path of the vendor sdram_init()) */
void sdram_init(void)
{
	ddr_clk_set_rate();
	reset_dll();

	reset_controller();

	ddr_inno_phy_init();

	ddr_controller_init();

	/* open remap function */
	mem_remap();
	/* must modify after opening remap function */
	ddr_writel(DDRC_CTRL_VALUE & 0xffff07ff, DDRC_CTRL);

	ddr_writel(ddr_readl(DDRC_STATUS) & ~DDRC_DSTATUS_MISS, DDRC_STATUS);
	ddr_writel(0, DDRC_DLP);
}
