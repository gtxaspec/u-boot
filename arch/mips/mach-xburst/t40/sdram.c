// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T40 DDR2 controller and Innophy PHY init (SPL)
 *
 * Structural port of the T31 DDR2 path (mach-xburst/t31/sdram.c),
 * which itself is a faithful transliteration of the vendor known-
 * good ddr_innophy.c / ddr_set_dll.c / clk.c. T40 uses the same
 * legacy Ingenic DDRC + Innophy DDR2 training PHY as T31, the
 * default 64 MB M14D5121632A chip and a similar ~500 MHz clock
 * (MPLL 1000 / 2). 16-bit bus, CS0 only.
 *
 * Untested on real T40 silicon - parameter values come from the
 * T31 DDR2-@500 set (same DRAM chip, ~same clock) and need a
 * cross-check against vendor T40 ddr_dwc.c on the lab T40 unit.
 * The register write order / poll loops / delays match the
 * vendor and the working T31 port.
 */

#include <asm/io.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <mach/t40.h>
#include <mach/t40-ddr.h>

void t40_spl_puts(const char *s);

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
 * clk_set_rate(). Source is MPLL (1200 MHz), target 600 MHz, so
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

	t40_spl_puts("cal1\n");
	m = phy_readl(INNO_TRAINING_CTRL);
	m = 0xa1;
	phy_writel(m, INNO_TRAINING_CTRL);
	t40_spl_puts("cal2 trig\n");
	while (0x3 != readl((void __iomem *)(DDR_PHY_BASE + 0xcc)))
		;
	t40_spl_puts("cal3 done\n");
	phy_writel(0xa0, INNO_TRAINING_CTRL);
	t40_spl_puts("cal4 off\n");
}

/* read-modify-write a PHY register bit range (idx is the reg index, *4 */
static void phy_set_range(u32 idx, u32 start, u32 num, u32 val)
{
	void __iomem *p = (void __iomem *)(DDR_PHY_BASE + idx * 4);
	u32 mask = ((1u << num) - 1) << start;
	u32 v = readl(p);

	v = (v & ~mask) | ((val << start) & mask);
	writel(v, p);
}

/* T40N driver-strength + on-die termination - vendor T40N branch of
 * ddr_phy_cfg_driver_odt(). Required for signal integrity; without
 * this, DDR2 reads come back as garbage even with the controller and
 * PHY correctly programmed. */
static void t40n_phy_driver_odt(void)
{
	const u32 drvcmd = 0x4, drvclk = 0x9;
	const u32 odt = 0x5, drval = 0x3, drvah = 0x3;

	/* cmd / ck drive strength */
	phy_set_range(0xb0, 0, 5, drvcmd);
	phy_set_range(0xb1, 0, 5, drvcmd);
	phy_set_range(0xb2, 0, 5, drvclk);
	phy_set_range(0xb3, 0, 5, drvclk);
	/* DQ ODT, channel A */
	phy_set_range(0xc0, 0, 5, odt);
	phy_set_range(0xc1, 0, 5, odt);
	phy_set_range(0xd0, 0, 5, odt);
	phy_set_range(0xd1, 0, 5, odt);
	/* DQ ODT, channel B */
	phy_set_range(0xe0, 0, 5, odt);
	phy_set_range(0xe1, 0, 5, odt);
	phy_set_range(0xf0, 0, 5, odt);
	phy_set_range(0xf1, 0, 5, odt);
	/* DQ driver strength, channel A */
	phy_set_range(0xc2, 0, 5, drval);
	phy_set_range(0xc3, 0, 5, drval);
	phy_set_range(0xd2, 0, 5, drvah);
	phy_set_range(0xd3, 0, 5, drvah);
	/* DQ driver strength, channel B */
	phy_set_range(0xe2, 0, 5, drval);
	phy_set_range(0xe3, 0, 5, drval);
	phy_set_range(0xf2, 0, 5, drvah);
	phy_set_range(0xf3, 0, 5, drvah);
}

static void ddr_inno_phy_init(void)
{
	u32 __maybe_unused reg = 0;	/* used only by the DDR2 path */

	t40_spl_puts("phy1\n");
	/* Vendor T40 ddr_phy_init: reset PHY then config driver/ODT
	 * BEFORE programming PLLs / CL / CWL. PHY register indices are
	 * 4-byte-aligned; vendor writes use `0xb3011000 + idx*4` so we
	 * use phy_writel(v, idx*4). */
	phy_writel(0x0d, 0x00);				/* INNO_PHY_RST */
	t40n_phy_driver_odt();
	/* Vendor T40N DLL bypass values (ddr_phy_init T40N branch). */
	phy_writel(0xc, 0x54 * 4);	/* byte0 dq dll */
	phy_writel(0xc, 0x64 * 4);	/* byte1 dq dll */
	phy_writel(0xc, 0x84 * 4);	/* byte2 dq dll */
	phy_writel(0xc, 0x94 * 4);	/* byte3 dq dll */
	phy_writel(0x1, 0x55 * 4);	/* byte0 dqs dll */
	phy_writel(0x1, 0x65 * 4);	/* byte1 dqs dll */
	phy_writel(0x1, 0x85 * 4);	/* byte2 dqs dll */
	phy_writel(0x1, 0x95 * 4);	/* byte3 dqs dll */
	phy_writel(0xc, 0x15 * 4);	/* cmd dll */
	phy_writel(0x1, 0x16 * 4);	/* ck dll */

	phy_writel(0x10, INNO_PLL_FBDIV);
	phy_writel(0x1a, INNO_PLL_CTRL);
	phy_writel(0x4, INNO_PLL_PDIV);
	phy_writel(0x18, INNO_PLL_CTRL);
	t40_spl_puts("phy2 plls\n");

	while (!(phy_readl(INNO_PLL_LOCK) & (1 << 3)))	/* wait pll lock */
		;
	t40_spl_puts("phy3 lock\n");

	phy_writel(0x0, INNO_TRAINING_CTRL);
#if CONFIG_DDR_DW32
	phy_writel(0x0f, INNO_DQ_WIDTH);	/* 32-bit DQ */
#else
	phy_writel(0x03, INNO_DQ_WIDTH);	/* 16-bit DQ */
#endif

#if defined(CONFIG_T31_DDR3)
	/* MEMSEL = DDR3, BURSEL = burst8 */
	phy_writel(0x30, INNO_MEM_CFG);
	/* DQS0/1 TXPLL: clear [6:4] (vendor non-T23 path, raw 0x154/0x114) */
	phy_writel(phy_readl(0x154) & 0xffffff8f, 0x154);
	phy_writel(phy_readl(0x114) & 0xffffff8f, 0x114);
	phy_writel(0x0d, INNO_CHANNEL_EN);
	phy_writel(0x6, INNO_CWL);
	phy_writel(0x8, INNO_CL);
	phy_writel(0x00, INNO_AL);
#else
	/* MEMSEL = DDR2, BURSEL = burst8 */
	phy_writel(0x11, INNO_MEM_CFG);
	phy_writel(0x0d, INNO_CHANNEL_EN);
	phy_writel(((DDRP_MR0_VALUE & 0xf0) >> 4) - 1, INNO_CWL);
	reg = ((DDRP_MR0_VALUE & 0xf0) >> 4);
	phy_writel(reg, INNO_CL);
	phy_writel(0x00, INNO_AL);
#endif

	t40_spl_puts("phy4 mr\n");
	/*
	 * Vendor T40 DFI init (ddrc_dfi_init in arch/mips/cpu/xburst2/
	 * ddr_innophy.c): write DFI_INIT_START bit to DWCFG with the
	 * buswidth bit (16-bit = 0); clear DWCFG; poll DWSTATUS for
	 * DFI_INIT_COMP. Our earlier code wrote to PHY_INIT (0x8c) and
	 * polled the wrong bit - PHY_INIT is a different register, the
	 * actual handshake is on DWCFG/DWSTATUS.
	 */
#if CONFIG_DDR_DW32
	writel(DDRC_DWCFG_DFI_INIT_START | 1, (void __iomem *)DDRC_DWCFG);
	writel(1, (void __iomem *)DDRC_DWCFG);	/* buswidth = 32-bit (bit 0 = 1) */
#else
	writel(DDRC_DWCFG_DFI_INIT_START, (void __iomem *)DDRC_DWCFG);
	writel(0, (void __iomem *)DDRC_DWCFG);	/* buswidth = 16-bit (bit 0 = 0) */
#endif
	while (!(readl((void __iomem *)DDRC_DWSTATUS) & DDRC_DWSTATUS_DFI_INIT_COMP))
		;
	t40_spl_puts("phy5 dfi-init\n");
	udelay(50);
	writel(0, (void __iomem *)REG_DDR_CTRL);

#if defined(CONFIG_T31_DDR3)
	/* DDR3: DFI reset (kgdreset) - set, 200us, clear, 500us. */
	writel(DDRC_CTRL_DFI_RST, (void __iomem *)REG_DDR_CTRL);
	udelay(200);
	writel(0, (void __iomem *)REG_DDR_CTRL);
	udelay(500);
#endif

	writel(DDRC_CFG_VALUE, (void __iomem *)REG_DDR_CFG);
	writel(0x0a, (void __iomem *)REG_DDR_CTRL);

#if defined(CONFIG_T31_DDR3)
	/* DDR3 LMR MRS sequence: MR2,MR3,MR1,MR0,ZQCL (no-poll
	 * writel pairs - vendor ddr_innophy.c DDR3 branch). */
	writel((0x08 << 12) | 0x211, (void __iomem *)REG_DDR_LMR);
	writel(0, (void __iomem *)REG_DDR_LMR);
	writel(0x311, (void __iomem *)REG_DDR_LMR);
	writel(0, (void __iomem *)REG_DDR_LMR);
	writel((0x6 << 12) | 0x111, (void __iomem *)REG_DDR_LMR);
	writel(0, (void __iomem *)REG_DDR_LMR);
	writel((DDRP_MR0_VALUE << 12) | 0x011, (void __iomem *)REG_DDR_LMR);
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
#else
	/*
	 * DDR2 LMR sequence - vendor T40 ddr_innophy.c ddrc_dfi_init()
	 * DDR2 branch (udelay-paced, no polls; the poll-based T31
	 * sequence stalls because bit 0 doesn't auto-clear here).
	 *
	 * Vendor LMR cmd encoding for MRn (where DDR_MR{n}_VALUE is
	 * MRn's mode-register value):
	 *   cmd = (1 << 1) | START(bit 0) | LMR_CMD |
	 *         ((mr & 0x1fff) << DDR_ADDR_BIT) |
	 *         (((mr >> 13) & 0x3) << BA_BIT)
	 * Vendor START bit = bit 0, LMR_CMD constant = 0x011 (PCHG=
	 * 0x400001/0x400003, REF=0x400009, LMR=0x011 in the cmd field).
	 */
	t40_spl_puts("lmr1\n");
	writel(0x400003, (void __iomem *)REG_DDR_LMR);
	udelay(100);
	t40_spl_puts("lmr2\n");
	writel(0x211, (void __iomem *)REG_DDR_LMR);	/* MR2 */
	udelay(5);
	t40_spl_puts("lmr3\n");
	writel(0x311, (void __iomem *)REG_DDR_LMR);	/* MR3 */
	udelay(5);
	t40_spl_puts("lmr4\n");
	writel(0x111, (void __iomem *)REG_DDR_LMR);	/* MR1 */
	udelay(5);
	t40_spl_puts("lmr5\n");
	reg = ((DDRP_MR0_VALUE & 0x1fff) << 12) | 0x011;
	writel(reg, (void __iomem *)REG_DDR_LMR);	/* MR0 */
	udelay(5);
	t40_spl_puts("lmr6\n");
	writel(0x400003, (void __iomem *)REG_DDR_LMR);	/* PCHG */
	udelay(5);
	writel(0x400009, (void __iomem *)REG_DDR_LMR);	/* REF */
	udelay(5);
	writel(0x400009, (void __iomem *)REG_DDR_LMR);	/* REF */
	udelay(5 * 1000);
	t40_spl_puts("lmr7\n");
#endif

	/* Vendor T40 skips phy_calibration in the default config; the
	 * call here came from the T31 path. Skip until we know it's
	 * needed. */
	t40_spl_puts("phy-cal skipped\n");
}

/* DDR2 sdram init (innophy DDR2 path of the vendor sdram_init()) */
void sdram_init(void)
{
	t40_spl_puts("sdram1\n");
	ddr_clk_set_rate();
	t40_spl_puts("sdram2 clk\n");
	reset_dll();
	t40_spl_puts("sdram3 dll\n");

	reset_controller();
	t40_spl_puts("sdram4 rst\n");

	ddr_inno_phy_init();
	t40_spl_puts("sdram5 phy\n");

	ddr_controller_init();
	t40_spl_puts("sdram6 ctrl\n");

	/* open remap function */
	mem_remap();
	t40_spl_puts("sdram7 remap\n");
	/* must modify after opening remap function */
	ddr_writel(DDRC_CTRL_VALUE & 0xffff07ff, DDRC_CTRL);

	ddr_writel(ddr_readl(DDRC_STATUS) & ~DDRC_DSTATUS_MISS, DDRC_STATUS);
	ddr_writel(0, DDRC_DLP);
	t40_spl_puts("sdram8 done\n");
}
