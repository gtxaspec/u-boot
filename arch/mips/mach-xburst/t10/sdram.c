// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T10 DDR2 controller and Synopsys DWC PHY init (SPL)
 *
 * Faithful transliteration of the vendor known-good DWC DDR2 path
 * (arch/mips/cpu/xburst/ddr_dwc.c + t10/ddr_set_dll.c + the DDR
 * branch of t10/clk.c clk_set_rate) for the isvp_t10 (T10N)
 * profile: DDR2 M14D5121632A, 64 MB, 16-bit, single CS0, 4-bank,
 * DDR clock 500 MHz (MPLL 1000 / 2, so cdr = 1, NOT bypass).
 *
 * T10 is the only XBurst1 camera SoC that is NOT Innophy: it uses
 * the Synopsys DWC PHY with hardware ZQ impedance calibration and
 * a hardware DQS training engine. The vendor flow is reproduced
 * exactly (register write order, the prev-DDR-init DLL reset hook,
 * the PIR/PGSR poll loops); only the DM clk_set_rate() call is
 * inlined as a direct CPM_DDRCDR divider write (the SPL has no
 * driver model). bypass is hard 0 (DDR 500 MHz > 200 MHz) and the
 * DRAM is always DDR2, so the LPDDR/DDR3/bypass branches of the
 * vendor driver are dropped. All register VALUEs live in
 * mach/t10-ddr.h (exact ddr_params_creator output).
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <mach/t10.h>
#include <mach/t10-ddr.h>

void t10_spl_puts(const char *s);

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

static const u32 out_imp_table[] = DDRP_IMPANDCE_ARRAY;
static const u32 odt_imp_table[] = DDRP_ODT_IMPANDCE_ARRAY;
static const u8  rzq_table[]     = DDRP_RZQ_TABLE;
static const u32 remap_array[]   = REMMAP_ARRAY;

static void ddr_die(const char *what)
{
	t10_spl_puts("T10 SPL: DDR FAIL ");
	t10_spl_puts(what);
	t10_spl_puts("\n");
	for (;;)
		;
}

/*
 * DDR clock divider: the DDR branch of the vendor clk_set_rate().
 * Source MPLL (1000 MHz), target 500 MHz, so
 * cdr = ((pll + rate - 1) / rate - 1) & 0xff = 1. The T10 CGU
 * entry is {en, CPM_DDRCDR, sel_bit=30, MPLL, sel[], ce=29,
 * busy=28, stop=27}: the PLL-select bits [31:30] are left as the
 * mask ROM set them (DDR must already run off a PLL to have
 * reached here); we only clear the divider field (low nibble +
 * 0x3f<<24) and set CE (bit 29) + cdr, then poll BUSY (bit 28).
 */
static void ddr_clk_set_rate(void)
{
	unsigned int pll_rate = DDR_MPLL_RATE;
	unsigned int rate = DDR_TARGET_RATE;
	unsigned int cdr;
	u32 regval;

	cdr = ((pll_rate + rate - 1) / rate - 1) & 0xff;

	regval = cpm_readl(CPM_DDRCDR);
	regval &= ~(0xf | (0x3f << 24));
	regval |= ((1 << 29) | cdr);		/* ce = bit 29 */
	cpm_writel(regval, CPM_DDRCDR);
	while (cpm_readl(CPM_DDRCDR) & (1 << 28))	/* busy = bit 28 */
		;
}

/*
 * prev_ddr_init hook from t10/ddr_set_dll.c (reset_dllA).
 * WARNING (vendor, 2015-01-08): CPM_DRCG (0xb00000d0) BIT6 must
 * stay set (0x40); clearing it makes chip memory unstable and the
 * GPU hangs.
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

static void mem_remap(void)
{
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(remap_array); i++)
		ddr_writel(remap_array[i], DDRC_REMAP(i + 1));
}

static void ddr_controller_init(void)
{
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
	ddr_writel(DDRC_CTRL_VALUE, DDRC_CTRL);

	mem_remap();

	ddr_writel(ddr_readl(DDRC_STATUS) & ~DDRC_DSTATUS_MISS, DDRC_STATUS);
	/* DDRC_AUTOSR_EN_VALUE == 0: the autosr/DLP branch is a no-op. */
	ddr_writel(DDRC_AUTOSR_EN_VALUE, DDRC_AUTOSR_EN);
}

/* DDR2 path of the vendor ddr_phy_param_config(). */
static void ddr_phy_param_config(void)
{
	phy_writel(DDRP_DCR_VALUE, DDRP_DCR);
	/* vendor leaves ODTCR commented out */
	phy_writel(DDRP_PTR0_VALUE, DDRP_PTR0);
	phy_writel(DDRP_PTR1_VALUE, DDRP_PTR1);
	phy_writel(DDRP_PTR2_VALUE, DDRP_PTR2);
	phy_writel(DDRP_DTPR0_VALUE, DDRP_DTPR0);
	phy_writel(DDRP_DTPR1_VALUE, DDRP_DTPR1);
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

static void ddr_phy_init(void)
{
	phy_writel(0x150000, DDRP_DTAR);	/* training address */
	ddr_phy_param_config();
	ddr_phy_init_dram();
	ddr_training_hardware();
}

static void controller_reset_phy(void)
{
	ddr_writel(0xf << 20, DDRC_CTRL);
	mdelay(1);
	ddr_writel(0, DDRC_CTRL);
	mdelay(1);
	/* force CKE1/CS1 + CS0 high */
	ddr_writel(DDRC_CFG_VALUE | DDRC_CFG_CS1EN | DDRC_CFG_CS0EN, DDRC_CFG);
	ddr_writel((1 << 1), DDRC_CTRL);
}

void sdram_init(void)
{
	ddr_clk_set_rate();
	reset_dllA();			/* prev_ddr_init hook */
	controller_reset_phy();
	ddr_phy_init();
	ddr_controller_init();
}
