// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T41 DDR3 controller + Innophy PHY init (SPL)
 *
 * Faithful port of vendor T41-1.2.6 `arch/mips/cpu/xburst2/ddr_innophy.c`
 * sdram_init() DDR3 path for T41NQ (W631GU6NG 128 MiB 16-bit @ 700 MHz).
 *
 * T41 has a DIFFERENT Innophy PHY revision from T40 - register offsets
 * are reshuffled (PLL regs at 0x140+ vs T40's 0x80+, PLL_LOCK at 0x180
 * vs T40's 0xc8, DQ_WIDTH at 0x034 vs T40's 0x7c). This file uses
 * absolute PHY addresses from vendor `arch/mips/include/asm/ddr_innophy.h`
 * on the T41-1.2.6 branch.
 *
 * DDRC register values in <mach/t41nq-ddr.h> come from vendor
 * `ddr_params_creator` with T41NQ + W631GU6NG + DDR=700 + DW32=0.
 */

#include <asm/io.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <mach/t41.h>
#include <mach/t41-ddr.h>

void t41_spl_puts(const char *s);

#define ddr_writel(v, off)	writel((v), (void __iomem *)(DDRC_BASE + (off)))
#define ddr_readl(off)		readl((void __iomem *)(DDRC_BASE + (off)))

/* T41 Innophy PHY registers (offsets from DDRC_BASE + DDR_PHY_OFFSET) */
#define PHY_RST			(DDR_PHY_OFFSET + 0x000)
#define PHY_MEM_CFG		(DDR_PHY_OFFSET + 0x004)
#define PHY_TRAINING_CTRL	(DDR_PHY_OFFSET + 0x008)
#define PHY_CL			(DDR_PHY_OFFSET + 0x014)
#define PHY_AL			(DDR_PHY_OFFSET + 0x018)
#define PHY_CWL			(DDR_PHY_OFFSET + 0x01c)
#define PHY_DQ_WIDTH		(DDR_PHY_OFFSET + 0x034)
#define PHY_PLL_FBDIVL		(DDR_PHY_OFFSET + 0x140)
#define PHY_PLL_FBDIVH		(DDR_PHY_OFFSET + 0x144)
#define PHY_PLL_PDIV		(DDR_PHY_OFFSET + 0x148)
#define PHY_PLL_CTRL		(DDR_PHY_OFFSET + 0x14c)
#define PHY_PLL_LOCK		(DDR_PHY_OFFSET + 0x180)
#define PHY_CALIB_DONE		(DDR_PHY_OFFSET + 0x184)

/* DDRC APB region registers (offsets from DDRC_BASE) */
#define DDRC_APB_OFF		(-0x4e0000 + 0x2000)
#define APB_DWCFG		(DDRC_APB_OFF + 0x00)
#define APB_DWSTATUS		(DDRC_APB_OFF + 0x04)
#define APB_REMAP(n)		(DDRC_APB_OFF + 0x08 + 4 * ((n) - 1))
#define APB_CGUC0		(DDRC_APB_OFF + 0x64)
#define APB_CGUC1		(DDRC_APB_OFF + 0x68)
#define APB_PREGPRO		(DDRC_APB_OFF + 0x6c)

#define DWCFG_DFI_INIT_START	(1 << 3)
#define DWSTATUS_DFI_INIT_COMP	(1 << 0)
#define CTRL_CKE		(1 << 1)
#define LMR_START		(1 << 0)
#define LMR_CMD_LMR		(2 << 6)
#define LMR_CMD_ZQCL_CS0	(3 << 6)
#define LMR_ADDR_BIT		12
#define LMR_BA_BIT		9

static u32 cpm_readl(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void cpm_writel(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

static void ddr_clk_set_rate(void)
{
	unsigned int pll_rate = DDR_MPLL_RATE;
	unsigned int rate = DDR_TARGET_RATE;
	unsigned int cdr;
	u32 regval;

	/*
	 * Vendor clk_init() -> cgu_clks_set() does a full CGU reset
	 * cycle on every peripheral clock before programming:
	 *   1. Set max divider + ce, poll busy
	 *   2. Stop clock + ce, poll busy
	 *   3. Clear ce + stop
	 *   4. Set PLL source select (MPLL for DDR)
	 *   5. Program actual divider + ce, poll busy
	 *
	 * Steps 1-3 reset the CGU state machine. Without this, the
	 * DDR clock may be in an undefined state from bootrom.
	 */

	/* Step 1: max divider + ce */
	regval = cpm_readl(CPM_DDRCDR);
	regval |= 0xff | (1 << 29);	/* max div + ce */
	cpm_writel(regval, CPM_DDRCDR);
	while (cpm_readl(CPM_DDRCDR) & (1 << 28))
		;

	/* Step 2: stop clock + ce */
	regval = cpm_readl(CPM_DDRCDR);
	regval |= (1 << 27) | (1 << 29);	/* stop + ce */
	cpm_writel(regval, CPM_DDRCDR);
	while (cpm_readl(CPM_DDRCDR) & (1 << 28))
		;

	/* Step 3: clear ce + stop */
	regval = cpm_readl(CPM_DDRCDR);
	regval &= ~((1 << 29) | (1 << 27));
	cpm_writel(regval, CPM_DDRCDR);

	/* Step 4: PLL source = MPLL (bits 31:30 = 2) */
	regval = cpm_readl(CPM_DDRCDR);
	regval &= ~(3 << 30);
	regval |= (2 << 30);
	cpm_writel(regval, CPM_DDRCDR);

	/* Step 5: actual divider */
	regval = cpm_readl(CPM_DDRCDR);
	if (pll_rate % rate >= rate / 2)
		pll_rate += rate - (pll_rate % rate);
	else
		pll_rate -= (pll_rate % rate);
	cdr = (pll_rate / rate - 1) & 0xff;
	regval &= ~(0xf | (0x3f << 24));
	regval |= ((1 << 29) | cdr);
	cpm_writel(regval, CPM_DDRCDR);
	while (cpm_readl(CPM_DDRCDR) & (1 << 28))
		;
}

static void ddrc_reset_phy(void)
{
	ddr_writel(0xf << 20, DDRC_CTRL);
	mdelay(1);
	ddr_writel(0x8 << 20, DDRC_CTRL);
	mdelay(1);
}

static void ddr_phy_init(void)
{
	u32 val;
	int i = 0;

	val = ddr_readl(PHY_PLL_FBDIVL);
	val &= ~0xff;
	val |= 0x1;
	ddr_writel(val, PHY_PLL_FBDIVL);

	val = ddr_readl(PHY_PLL_FBDIVH);
	val &= ~0xff;
	val |= 0x80;
	ddr_writel(val, PHY_PLL_FBDIVH);

	/*
	 * Vendor starts with pll_sel=0 (0x28/0x20 for >625MHz), retries
	 * with pll_sel=1 (0x48/0x40) if PLL doesn't lock within 500
	 * iterations. On our T41NQ lab board pll_sel=0 consistently fails
	 * (LOCK reads 0x12, bit 2 never sets) and pll_sel=1 works. The
	 * stock vendor SPL shows the same pattern in its boot log:
	 *   DDRP_INNOPHY_PLL_CTRL: 00000028  (pll_sel=0, first try)
	 *   DDRP_INNOPHY_PLL_CTRL: 00000020  (pll_sel=0, fails)
	 * then silently retries to pll_sel=1 which locks.
	 *
	 * Implement the vendor retry loop: try pll_sel=0, if PLL doesn't
	 * lock in 500 polls, reset PHY and retry with pll_sel=1.
	 */
	{
		int pll_sel;
		for (pll_sel = 0; pll_sel < 2; pll_sel++) {
			val = ddr_readl(PHY_PLL_CTRL);
			val &= ~0xff;
			val |= (pll_sel ? 0x48 : 0x28);
			ddr_writel(val, PHY_PLL_CTRL);
			udelay(500);

			val = ddr_readl(PHY_PLL_PDIV);
			val &= ~0x1f;
			val |= 0x1;
			ddr_writel(val, PHY_PLL_PDIV);

			val = pll_sel ? 0x40 : 0x20;
			ddr_writel(val, PHY_PLL_CTRL);
			udelay(500);

			i = 0;
			while (!(ddr_readl(PHY_PLL_LOCK) & (1 << 2))) {
				if (++i > 500) break;
			}
			if (i <= 500) break;

			ddrc_reset_phy();
			val = ddr_readl(PHY_PLL_FBDIVL);
			val &= ~0xff; val |= 0x1;
			ddr_writel(val, PHY_PLL_FBDIVL);
			val = ddr_readl(PHY_PLL_FBDIVH);
			val &= ~0xff; val |= 0x80;
			ddr_writel(val, PHY_PLL_FBDIVH);
		}
	}

	/* PLL lock poll + retry is handled in the pll_sel loop above */

	val = ddr_readl(PHY_MEM_CFG);
	val &= ~0xff;
	val |= DDRP_MEMCFG_VALUE;
	ddr_writel(val, PHY_MEM_CFG);

	ddr_writel(0x3, PHY_DQ_WIDTH);

	val = ddr_readl(PHY_RST);
	val &= ~0xff;
	val |= 0x0d;
	ddr_writel(val, PHY_RST);

	val = ddr_readl(PHY_CWL);
	val &= ~0xf;
	val |= DDRP_CWL_VALUE;
	ddr_writel(val, PHY_CWL);

	val = ddr_readl(PHY_CL);
	val &= ~0xf;
	val |= DDRP_CL_VALUE;
	ddr_writel(val, PHY_CL);

	ddr_writel(0x0, PHY_AL);
}

static void ddrp_set_drv_odt(void)
{
	void __iomem *phy = (void __iomem *)(DDRC_BASE + DDR_PHY_OFFSET);

	writel(0x00, phy + 0x140 * 4);
	writel(0x00, phy + 0x141 * 4);
	writel(0x00, phy + 0x150 * 4);
	writel(0x00, phy + 0x151 * 4);
	writel(0x0f, phy + 0x130 * 4);
	writel(0x0f, phy + 0x131 * 4);
	writel(0x03, phy + 0x132 * 4);
	writel(0x03, phy + 0x133 * 4);
	writel(0x14, phy + 0x142 * 4);
	writel(0x14, phy + 0x143 * 4);
	writel(0x14, phy + 0x152 * 4);
	writel(0x14, phy + 0x153 * 4);
}

static void ddrc_dfi_init(void)
{
	u32 kgd_rtt_dic = 0x02;

	ddr_writel(DWCFG_DFI_INIT_START, APB_DWCFG);
	ddr_writel(0, APB_DWCFG);
	while (!(ddr_readl(APB_DWSTATUS) & DWSTATUS_DFI_INIT_COMP))
		;
	ddr_writel(0, DDRC_CTRL);
	udelay(5);
	ddr_writel(DDRC_CFG_VALUE, DDRC_CFG);
	udelay(5);
	ddr_writel(CTRL_CKE, DDRC_CTRL);
	udelay(5);

#define _LMR(n) \
	(DDRC_DLMR_VALUE | LMR_START | LMR_CMD_LMR | \
	 ((DDR_MR##n##_VALUE & 0xffff) << LMR_ADDR_BIT) | \
	 (((DDR_MR##n##_VALUE >> 16) & 0x7) << LMR_BA_BIT))

	ddr_writel(0, DDRC_LMR); udelay(5);
	ddr_writel(_LMR(2), DDRC_LMR); udelay(5);
	ddr_writel(0, DDRC_LMR); udelay(5);
	ddr_writel(_LMR(3), DDRC_LMR); udelay(5);
	ddr_writel(0, DDRC_LMR); udelay(5);
	ddr_writel((_LMR(1) & ~0x266000) | (kgd_rtt_dic << 12), DDRC_LMR);
	udelay(5);
	ddr_writel(0, DDRC_LMR); udelay(5);
	ddr_writel(_LMR(0), DDRC_LMR); udelay(5);
	ddr_writel(DDRC_DLMR_VALUE | LMR_START | LMR_CMD_ZQCL_CS0, DDRC_LMR);
	udelay(5);
#undef _LMR
}

static void ddrc_prev_init(void)
{
	ddr_writel(DDRC_TIMING1_VALUE, DDRC_TIMING(1));
	ddr_writel(DDRC_TIMING2_VALUE, DDRC_TIMING(2));
	ddr_writel(DDRC_TIMING3_VALUE, DDRC_TIMING(3));
	ddr_writel(DDRC_TIMING4_VALUE, DDRC_TIMING(4));
	ddr_writel(DDRC_TIMING5_VALUE, DDRC_TIMING(5));
	ddr_writel(DDRC_MMAP0_VALUE, DDRC_MMAP0);
	ddr_writel(DDRC_MMAP1_VALUE, DDRC_MMAP1);
	ddr_writel(DDRC_CTRL_VALUE & ~(7 << 12), DDRC_CTRL);
}

static void ddrp_hardware_calibration(void)
{
	u32 val;
	int timeout = 1000000;

	ddr_writel(0x0, PHY_TRAINING_CTRL);
	ddr_readl(PHY_TRAINING_CTRL);
	ddr_writel(0x1, PHY_TRAINING_CTRL);
	do {
		val = ddr_readl(PHY_CALIB_DONE);
	} while (((val & 0xf) != 0x3) && timeout--);
	if (!timeout) {
		t41_spl_puts("DDR: calibration timeout\n");
	}
	ddr_writel(0x0, PHY_TRAINING_CTRL);
}

static void remap_swap(int a, int b)
{
	u32 remmap[2], tmp[2];

	remmap[0] = ddr_readl(APB_REMAP(a / 4 + 1));
	remmap[1] = ddr_readl(APB_REMAP(b / 4 + 1));
#define BIT_OF(bit) (((bit) % 4) * 8)
#define MASK_OF(bit) (0x1f << BIT_OF(bit))
	tmp[0] = (remmap[0] & MASK_OF(a)) >> BIT_OF(a);
	tmp[1] = (remmap[1] & MASK_OF(b)) >> BIT_OF(b);
	remmap[0] &= ~MASK_OF(a);
	remmap[1] &= ~MASK_OF(b);
	ddr_writel(remmap[0] | (tmp[1] << BIT_OF(a)), APB_REMAP(a / 4 + 1));
	ddr_writel(remmap[1] | (tmp[0] << BIT_OF(b)), APB_REMAP(b / 4 + 1));
#undef BIT_OF
#undef MASK_OF
}

static void mem_remap(void)
{
	u32 start = 0, num = 0;

	start += DDR_ROW + DDR_COL + (CONFIG_DDR_DW32 ? 4 : 2) / 2;
	start -= 12;
	if (DDR_BANK8) num += 3; else num += 2;
	if (CONFIG_DDR_CS0 && CONFIG_DDR_CS1) num++;
	for (; num > 0; num--)
		remap_swap(num - 1, start + num - 1);
}

static void ddrc_post_init(void)
{
	ddr_writel(DDRC_REFCNT_VALUE, DDRC_REFCNT);
	mem_remap();
	/* Write CTRL with PDT disabled (bits 14:12 = 0) during init.
	 * PDT=3 (0xb092) enters power-down after 32 tCK idle, which
	 * drops CKE before the SPL can verify DRAM. Leave power-down
	 * disabled until after U-Boot proper takes over. */
	ddr_writel((DDRC_CTRL_VALUE & ~(7 << 12)) | CTRL_CKE, DDRC_CTRL);
	ddr_writel(DDRC_CGUC0_VALUE, APB_CGUC0);
	ddr_writel(DDRC_CGUC1_VALUE, APB_CGUC1);
}

static void ddr_set_vref_skew(void)
{
	static const int dqx_rx[] = {
		0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10,
		0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x23, 0x25,
	};
	static const int dqx_tx[] = {
		0x03, 0x05, 0x07, 0x09, 0x0b, 0x0d, 0x0f, 0x11,
		0x18, 0x1a, 0x1c, 0x1e, 0x20, 0x22, 0x24, 0x26,
	};
	void __iomem *phy = (void __iomem *)(DDRC_BASE + DDR_PHY_OFFSET);
	int i;
	u32 wl;

	writel(0x8b, phy + 0x147 * 4);
	writel(0x8b, phy + 0x157 * 4);

	writel(0x11, phy + (0x1c0 + 0x00) * 4);
	writel(0x11, phy + (0x1c0 + 0x12) * 4);
	writel(0x11, phy + (0x1c0 + 0x2a) * 4);
	writel(0x11, phy + (0x1c0 + 0x15) * 4);
	writel(0x11, phy + (0x1c0 + 0x27) * 4);
	writel(0x11, phy + (0x1c0 + 0x2b) * 4);
	for (i = 0; i < 16; i++)
		writel(0x0a, phy + (0x1c0 + dqx_rx[i]) * 4);

	wl = readl(phy + 0x02 * 4);
	wl |= 0x8;
	writel(wl, phy + 0x02 * 4);

	writel(0x08, phy + (0x1c0 + 0x01) * 4);
	writel(0x08, phy + (0x1c0 + 0x13) * 4);
	writel(0x08, phy + (0x1c0 + 0x14) * 4);
	writel(0x08, phy + (0x1c0 + 0x16) * 4);
	writel(0x08, phy + (0x1c0 + 0x28) * 4);
	writel(0x08, phy + (0x1c0 + 0x29) * 4);
	for (i = 0; i < 0x1d; i++)
		writel(0x08, phy + (0x340 + i) * 4);
	writel(0x08, phy + (0x340 + 0x1e) * 4);
	writel(0x08, phy + (0x340 + 0x1f) * 4);
	for (i = 0; i < 16; i++)
		writel(0x0a, phy + (0x1c0 + dqx_tx[i]) * 4);
}

void sdram_init(void)
{
	t41_spl_puts("DDR: clk\n");

	/* Full CGU init matching vendor clk_init() + clk_set_rate(DDR).
	 * The vendor calls clk_init() BEFORE sdram_init(), which:
	 *   1. Ungates SFC0/SFC1 in CLKGR0, LZMA/IVDC in CLKGR1
	 *   2. Resets all CGU dividers: max div + ce, poll busy,
	 *      stop + ce, poll busy, clear ce + stop
	 *   3. Selects PLL sources (MPLL for DDR, SFC, MACPHY)
	 * Without steps 1-3, the DDR clock CGU is in bootrom-left
	 * state and the DDRC can't drive CKE. */
	{
		u32 v;

		/* Ungate clocks vendor enables */
		v = cpm_readl(CPM_CLKGR0);
		v &= ~(CPM_CLKGR0_SFC | CPM_CLKGR0_DDR);
		cpm_writel(v, CPM_CLKGR0);

		v = cpm_readl(CPM_DRCG);
		cpm_writel(v | (1 << 6), CPM_DRCG);
	}

	ddr_clk_set_rate();

	t41_spl_puts("DDR: rst\n");
	ddrc_reset_phy();

	t41_spl_puts("DDR: phy\n");
	ddr_phy_init();

	t41_spl_puts("DDR: dfi\n");
	ddrc_dfi_init();

	t41_spl_puts("DDR: drv\n");
	ddrp_set_drv_odt();

	t41_spl_puts("DDR: ctrl\n");
	ddrc_prev_init();

	t41_spl_puts("DDR: cal\n");
	ddrp_hardware_calibration();

	t41_spl_puts("DDR: post\n");
	ddrc_post_init();

	ddr_writel(DDRC_AUTOSR_CNT_VALUE, DDRC_AUTOSR_CNT);
	ddr_writel(DDRC_AUTOSR_EN_VALUE ? 1 : 0, DDRC_AUTOSR_EN);
	ddr_writel(DDRC_HREGPRO_VALUE, DDRC_HREGPRO);
	ddr_writel(DDRC_PREGPRO_VALUE, APB_PREGPRO);

	writel(0x0, (void __iomem *)0xb301206c);
	writel(0xff000000, (void __iomem *)0xb3012040);
	writel(0x2bd07460, (void __iomem *)0xb3012048);
	writel(0x1, (void __iomem *)0xb301206c);

	t41_spl_puts("DDR: skew\n");
	ddr_set_vref_skew();
}
