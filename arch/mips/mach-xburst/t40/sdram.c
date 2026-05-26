// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T40 DDR2 controller and Innophy PHY init (SPL)
 *
 * Vendor-shaped port of `arch/mips/cpu/xburst2/ddr_innophy.c`
 * DDR2 path. T40N: 128 MiB M14D5121632A, 32-bit bus, single rank,
 * DDR @ 500 MHz from MPLL/2.
 *
 * The DDRC_*_VALUE / DDRP_*_VALUE constants in <mach/t40-ddr.h>
 * come from running the vendor `tools/ingenic-tools/ddr_params_
 * creator` host tool with the T40N + M14D5121632A_DDR2 inputs
 * and copying the generated `ddr_reg_values.h`.
 *
 * Sequence (from vendor sdram_init):
 *   ddr_clk_set_rate  (CPM_DDRCDR /3 from MPLL = 500 MHz)
 *   reset_dll         (CPM_DRCG kick)
 *   reset_controller  (DDRC_CTRL 0xf<<20 then 0)
 *   ddr_inno_phy_init (PHY reset + driver/ODT + DLL bypass + PHY
 *                      PLL + CL/CWL/AL + DFI handshake + LMR)
 *   ddr_controller_init  (= vendor ddrc_prev_init: TIMING + MMAP + CTRL)
 *   ddrp_hw_calibration  (DDR2 branch: 0xa9 -> poll 0xcc==0xf -> 0xa8)
 *   ddr_controller_post_init  (REFCNT + CTRL + CGUC0/1 + AUTOSR +
 *                              HREGPRO + PREGPRO)
 *   mem_remap         (vendor T40N REMMAP_ARRAY from params_creator)
 *   PHY DDR2 fixups   (PHY MEM_CFG=0x51, regs 0x8/0xa range writes)
 *   ddr_innophy_set_skew_t40n  (per-bit TX/RX delays for chA/chB)
 *
 * HW-validated on real T40NN silicon via USB-boot dev loop.
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

/*
 * Vendor T40 ddrc_reset_phy(): assert full reset (bits 23:20 = 0xf
 * = SR+DFI+ALH+SR_R), then drop to just DFI_RST asserted
 * (bit 23 = 0x8<<20 = dfi_reset_n LOW for Innophy) and HOLD that
 * state across PHY init. DFI_RST is released later in ddrc_dfi_init.
 *
 * Previously we cleared CTRL all the way to 0 here, which released
 * DFI_RST before PHY init ran - intermittent dram_verify failures
 * because the PHY occasionally raced the released DFI_RST.
 */
static void reset_controller(void)
{
	ddr_writel(0xf << 20, DDRC_CTRL);
	mdelay(1);
	ddr_writel(0x8 << 20, DDRC_CTRL);	/* dfi_reset_n low */
	mdelay(1);
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

/* Vendor T40 ddrc_prev_init() - TIMINGs + MMAPs + CTRL (with bits
 * 14:12 masked off; those are set later in post_init). */
static void ddr_controller_init(void)
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

/*
 * Vendor T40 ddrp_hardware_calibration(): kick training via
 * TRAINING_CTRL, poll INNO_CALIB_DONE bits[3:0] == 0xf, clear
 * training bit. Required so the PHY learns per-channel DQS/DQ skew
 * to the chip; without it writes get dropped on the floor.
 *
 * DDR2 path: write 0xa9 (training enable + DDR2 mode), poll, write
 * 0xa8 (clear enable).
 *
 * DDR3 path: set bit 0 of existing TRAINING_CTRL, poll, clear bit 0.
 */
static void ddrp_hw_calibration(void)
{
	int timeout = 1000000;

#if defined(CONFIG_T40_DDR3)
	phy_writel(phy_readl(INNO_TRAINING_CTRL) | 0x1, INNO_TRAINING_CTRL);
	while (((readl((void __iomem *)(DDR_PHY_BASE + 0xcc)) & 0xf) != 0xf) &&
	       timeout--)
		;
	phy_writel(phy_readl(INNO_TRAINING_CTRL) & ~0x1, INNO_TRAINING_CTRL);
#else
	phy_writel(0xa9, INNO_TRAINING_CTRL);
	while (((readl((void __iomem *)(DDR_PHY_BASE + 0xcc)) & 0xf) != 0xf) &&
	       timeout--)
		;
	phy_writel(0xa8, INNO_TRAINING_CTRL);
#endif
}

/*
 * Vendor T40 ddr_innophy_set_skew() T40N branch: per-bit TX/RX
 * delay defaults the PHY uses to time DQ/DQS at 500 MHz.
 */
static void ddr_innophy_set_skew_t40n(void)
{
	static const u32 dqx_rx[] = {
		0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
		0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,
	};
	static const u32 dqx_tx[] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
	};
	const u32 chA = 0x120, chB = 0x1a0;
	const u32 vref = 0x7f, dqs_rx = 0x18, dqx_rx_v = 0x11;
	const u32 dqs_tx = 0x07, dqx_tx_v = 0x0e;
	int i;

	/* vref - channels A and B */
	phy_writel(vref, 0xd7 * 4);
	phy_writel(vref, 0xd8 * 4);
	phy_writel(vref, 0xf7 * 4);
	phy_writel(vref, 0xf8 * 4);

	/* RX: DQS and DQ per channel */
	phy_writel(dqs_rx, (chA + 0x29) * 4);
	phy_writel(dqs_rx, (chA + 0x34) * 4);
	phy_writel(dqs_rx, (chB + 0x29) * 4);
	phy_writel(dqs_rx, (chB + 0x34) * 4);
	for (i = 0; i < 16; i++) {
		phy_writel(dqx_rx_v, (chA + dqx_rx[i]) * 4);
		phy_writel(dqx_rx_v, (chB + dqx_rx[i]) * 4);
	}

	/* TX: cmd (0x100..0x11e), then DM/DQS/DQSb per channel, then DQ */
	for (i = 0; i <= 0x1e; i++)
		phy_writel(dqs_tx, (0x100 + i) * 4);
	phy_writel(dqs_tx, (chA + 0x00) * 4);	/* dm0 */
	phy_writel(dqs_tx, (chA + 0x09) * 4);	/* dqs0 */
	phy_writel(dqs_tx, (chA + 0x0a) * 4);	/* dqsb0 */
	phy_writel(dqs_tx, (chA + 0x0b) * 4);	/* dm1 */
	phy_writel(dqs_tx, (chA + 0x14) * 4);	/* dqs1 */
	phy_writel(dqs_tx, (chA + 0x15) * 4);	/* dqsb1 */
	phy_writel(dqs_tx, (chB + 0x00) * 4);
	phy_writel(dqs_tx, (chB + 0x09) * 4);
	phy_writel(dqs_tx, (chB + 0x0a) * 4);
	phy_writel(dqs_tx, (chB + 0x0b) * 4);
	phy_writel(dqs_tx, (chB + 0x14) * 4);
	phy_writel(dqs_tx, (chB + 0x15) * 4);
	for (i = 0; i < 16; i++) {
		phy_writel(dqx_tx_v, (chA + dqx_tx[i]) * 4);
		phy_writel(dqx_tx_v, (chB + dqx_tx[i]) * 4);
	}
}

/*
 * Vendor T40 ddrc_post_init() - REFCNT (idempotent reapply), final
 * CTRL, CGUC0/1 clock-unit-gating; followed by AUTOSR + HREGPRO/
 * PREGPRO host/peripheral region protection.
 */
static void ddr_controller_post_init(void)
{
	ddr_writel(DDRC_REFCNT_VALUE, DDRC_REFCNT);
	ddr_writel(DDRC_CTRL_VALUE, DDRC_CTRL);
	ddr_writel(DDRC_CGUC0_VALUE, 0x064 + (DDR_APB_BASE - DDRC_BASE));
	ddr_writel(DDRC_CGUC1_VALUE, 0x068 + (DDR_APB_BASE - DDRC_BASE));

	ddr_writel(DDRC_AUTOSR_CNT_VALUE, DDRC_AUTOSR_CNT);
	ddr_writel(DDRC_AUTOSR_EN_VALUE ? 1 : 0, DDRC_AUTOSR_EN);

	ddr_writel(DDRC_HREGPRO_VALUE, DDRC_HREGPRO);
	ddr_writel(DDRC_PREGPRO_VALUE, 0x06c + (DDR_APB_BASE - DDRC_BASE));
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

	/*
	 * Vendor T40 ddr_phy_init DDR2 branch: write a uniform baseline
	 * delay of 5 to all cmd/DQ TX delay regs, and 2 to all DQS/DQSb
	 * TX delay regs. This is the FROM-RESET seed the per-bit auto
	 * calibration starts from; without this seed the per-bit delay
	 * regs come up with random values and the PHY hardware calibration
	 * has to work harder (or fails) on quick power cycles.
	 *
	 * Empirically on real T40NN silicon, adding this seed took USB-
	 * boot quick-cycle reliability from intermittent to 4/4 success at
	 * 3-5 s power-off intervals.
	 */
#if !defined(CONFIG_T40_DDR3)
	{
		const u32 chA = 0x120, chB = 0x1a0;
		int i;

		/* cmd TX delay 0x100..0x11e */
		for (i = 0; i <= 0x1e; i++)
			phy_writel(5, (0x100 + i) * 4);
		/* DQ TX delay, low half [0..8] both channels */
		for (i = 0; i <= 0x8; i++) {
			phy_writel(5, (chA + i) * 4);
			phy_writel(5, (chB + i) * 4);
		}
		/* DQ TX delay, high half [0xb..0x13] both channels */
		for (i = 0xb; i <= 0x13; i++) {
			phy_writel(5, (chA + i) * 4);
			phy_writel(5, (chB + i) * 4);
		}
		/* DQS / DQSb TX delay (per-channel, byte 0 + byte 1) = 2 */
		phy_writel(2, (chA + 0x09) * 4);
		phy_writel(2, (chB + 0x09) * 4);
		phy_writel(2, (chA + 0x0a) * 4);
		phy_writel(2, (chB + 0x0a) * 4);
		phy_writel(2, (chA + 0x14) * 4);
		phy_writel(2, (chB + 0x14) * 4);
		phy_writel(2, (chA + 0x15) * 4);
		phy_writel(2, (chB + 0x15) * 4);
	}
#endif

	phy_writel(0x10, INNO_PLL_FBDIV);
	phy_writel(0x1a, INNO_PLL_CTRL);
	phy_writel(0x4, INNO_PLL_PDIV);
	phy_writel(0x18, INNO_PLL_CTRL);

	while (!(phy_readl(INNO_PLL_LOCK) & (1 << 3)))	/* wait pll lock */
		;

	phy_writel(0x0, INNO_TRAINING_CTRL);
#if CONFIG_DDR_DW32
	phy_writel(0x0f, INNO_DQ_WIDTH);	/* 32-bit DQ */
#else
	phy_writel(0x03, INNO_DQ_WIDTH);	/* 16-bit DQ */
#endif

	phy_writel(DDRP_MEMCFG_VALUE, INNO_MEM_CFG);
#if defined(CONFIG_T40_DDR3)
	/* DDR3: DQS0/1 TXPLL clear [6:4] (vendor non-T23 path, raw
	 * 0x154/0x114 byte offsets) */
	phy_writel(phy_readl(0x154) & 0xffffff8f, 0x154);
	phy_writel(phy_readl(0x114) & 0xffffff8f, 0x114);
#endif
	phy_writel(0x0d, INNO_CHANNEL_EN);
	phy_writel(DDRP_CWL_VALUE, INNO_CWL);
	phy_writel(DDRP_CL_VALUE, INNO_CL);
	phy_writel(0x00, INNO_AL);

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
	udelay(50);
	writel(0, (void __iomem *)REG_DDR_CTRL);

#if defined(CONFIG_T40_DDR3)
	/* DDR3: DFI reset (kgdreset) - set, 200us, clear, 500us. */
	writel(DDRC_CTRL_DFI_RST, (void __iomem *)REG_DDR_CTRL);
	udelay(200);
	writel(0, (void __iomem *)REG_DDR_CTRL);
	udelay(500);
#endif

	writel(DDRC_CFG_VALUE, (void __iomem *)REG_DDR_CFG);
	/* Vendor T40 ddrc_dfi_init: udelay(500) AFTER CFG write, BEFORE
	 * raising CKE - DRAM needs time to see configuration latched
	 * before CKE+commands are valid. Skipping this udelay made
	 * dram_verify intermittently fail. */
	udelay(500);
	writel(DDRC_CTRL_CKE, (void __iomem *)REG_DDR_CTRL);
	udelay(10);

#if defined(CONFIG_T40_DDR3)
	/*
	 * DDR3 LMR sequence per vendor T40 ddr_innophy.c DDR3 branch.
	 * Cmd encoding (DDR3-specific - DLMR_VALUE bit + 16-bit MR
	 * addr field + 3-bit BA):
	 *   cmd = DLMR_VALUE | START | LMR_CMD_LMR(2<<6) |
	 *         ((MR & 0xffff) << 12) | (((MR >> 16) & 0x7) << 9)
	 * DDRC_DLMR_VALUE for T40XP = 0x02 (from params_creator output).
	 *
	 * Order: zero, MR2, zero, MR3, zero, MR1, zero, MR0.
	 */
#define _LMR_MR3(mr_val) \
	(DDRC_DLMR_VALUE | (((mr_val) & 0xffff) << 12) | \
	 ((((mr_val) >> 16) & 0x7) << 9) | (2 << 6) | (1 << 0))

	writel(0, (void __iomem *)REG_DDR_LMR);			udelay(5);
	writel(_LMR_MR3(DDR_MR2_VALUE), (void __iomem *)REG_DDR_LMR);
	udelay(5);
	writel(0, (void __iomem *)REG_DDR_LMR);			udelay(5);
	writel(_LMR_MR3(DDR_MR3_VALUE), (void __iomem *)REG_DDR_LMR);
	udelay(5);
	writel(0, (void __iomem *)REG_DDR_LMR);			udelay(5);
	writel(_LMR_MR3(DDR_MR1_VALUE), (void __iomem *)REG_DDR_LMR);
	udelay(5);
	writel(0, (void __iomem *)REG_DDR_LMR);			udelay(5);
	writel(_LMR_MR3(DDR_MR0_VALUE), (void __iomem *)REG_DDR_LMR);
	udelay(5);
#undef _LMR_MR3
	udelay(1000);	/* DDR3 needs settle time after MR programming */

	/*
	 * Note: vendor T40 ddr_innophy.c has an explicit write-leveling
	 * sequence here (WR_LEVEL1=0x4, WR_LEVEL2=0x40, TRAINING_CTRL=
	 * 0xa4, poll WL_DONE == 0xf) but it's wrapped in `#if 0` -
	 * vendor skips DDR3 WL entirely; the per-channel DQ/DQS skew
	 * comes out of ddrp_hardware_calibration() called later. We
	 * follow the vendor's actual (#if 0 -> off) behavior.
	 */
#else
	/*
	 * DDR2 LMR sequence - vendor T40 ddr_innophy.c ddrc_dfi_init()
	 * DDR2 branch. Cmd encoding per vendor DDRC_LMR_MR(n) macro:
	 *   cmd = (1 << 1) | LMR_START(bit 0) | LMR_CMD_LMR(2<<6) |
	 *         ((MR & 0x1fff) << LMR_ADDR_BIT(12)) |
	 *         (((MR >> 13) & 0x3) << LMR_BA_BIT(9))
	 * PCHG-all = 0x400003 (vendor hardcoded literal). REF = 0x400009.
	 */
#define _LMR_MR(mr_val) \
	((((mr_val) & 0x1fff) << 12) | ((((mr_val) >> 13) & 0x3) << 9) | \
	 (2 << 6) | (1 << 1) | (1 << 0))

	writel(0x400003, (void __iomem *)REG_DDR_LMR);
	udelay(100);
	writel(_LMR_MR(DDR_MR2_VALUE), (void __iomem *)REG_DDR_LMR);
	udelay(5);
	writel(_LMR_MR(DDR_MR3_VALUE), (void __iomem *)REG_DDR_LMR);
	udelay(5);
	writel(_LMR_MR(DDR_MR1_VALUE), (void __iomem *)REG_DDR_LMR);
	udelay(5);
	writel(_LMR_MR(DDR_MR0_VALUE), (void __iomem *)REG_DDR_LMR);
	udelay(5);
	udelay(5 * 1000);	/* let MR0 DLL reset settle */
	writel(0x400003, (void __iomem *)REG_DDR_LMR);	/* PCHG all */
	udelay(100);
	writel(0x43, (void __iomem *)REG_DDR_LMR);	/* AUREF (CMD=1) */
	udelay(5);
	writel(0x43, (void __iomem *)REG_DDR_LMR);	/* AUREF again */
	udelay(5 * 1000);
#undef _LMR_MR
#endif

	/* Vendor T40 skips phy_calibration in the default config; the
	 * call here came from the T31 path. Skip until we know it's
	 * needed. */
}

/*
 * DDR2 sdram init - vendor-faithful port of
 * arch/mips/cpu/xburst2/ddr_innophy.c sdram_init(), DDR2 branch.
 *
 * Vendor T40 DDR2 sequence (verified against working vendor U-Boot
 * 2013 T40N SFCNOR binary that cold-boots reliably):
 *   clk_set_rate(DDR)
 *   ddrc_reset_phy()       - DDRC CTRL 0xf<<20 then 0x8<<20
 *   ddr_phy_init()         - PHY reset, ODT, DLL bypass, baseline
 *                            per-bit delay seed, PHY PLL lock
 *   ddrc_dfi_init()        - DFI handshake, CFG, CKE, LMR sequence
 *                            (we fold this into ddr_inno_phy_init)
 *   ddrc_prev_init()       - TIMING + MMAP + CTRL (bits 14:12 clear)
 *   ddrp_hardware_calibration()
 *   ddrc_post_init()       - REFCNT + CTRL + CGUC0/1
 *   AUTOSR + HREGPRO + PREGPRO
 *   PHY DDR2 fixups        - 0x51 to 0x1004, FIFO depth, TX wptr
 *   ddr_innophy_set_skew() - per-bit final skew values
 *
 * Things we used to do that vendor does NOT, and that broke quick-
 * cycle DDR reliability on T40NN:
 *   reset_dll() (CPM_DRCG toggle)
 *   udelay(1000) gaps between stages
 *   mem_remap (T40_REMMAP_ARRAY) for DDR2 (vendor's mem_remap is
 *     DDR3-only - DDR2 doesn't need it)
 *   final ddr_writel(CTRL & 0xffff07ff)
 *   final ddr_writel(STATUS & ~DDRC_DSTATUS_MISS)
 *   final ddr_writel(0, DDRC_DLP)
 *
 * All removed 2026-05-26.
 */
void sdram_init(void)
{
	ddr_clk_set_rate();

	reset_controller();		/* = vendor ddrc_reset_phy */

	ddr_inno_phy_init();		/* = vendor ddr_phy_init + ddrc_dfi_init */

	ddr_controller_init();		/* = vendor ddrc_prev_init */

	ddrp_hw_calibration();

	ddr_controller_post_init();	/* REFCNT + CTRL + CGUC0/1 + AUTOSR +
					 * HREGPRO + PREGPRO. Vendor does these
					 * in two steps; we combine. */

	/* Vendor mem_remap (REMMAP_ARRAY) is DDR3-only in the vendor
	 * source (#ifdef CONFIG_DDR_TYPE_DDR3). T40N DDR2 does not call
	 * it. Our prior code wrote T40_REMMAP_ARRAY unconditionally
	 * which is a no-op at best (the DDR2 memory map is the
	 * MMAP0/MMAP1 programming, not REMMAP) - removed. */

	/* Vendor T40 sdram_init() post-init PHY fixups (DDR-type
	 * specific). Without these the DQ FIFO and write-pointer
	 * alignment is off and reads return garbage. */
#if defined(CONFIG_T40_DDR3)
	phy_set_range(0x1, 6, 1, 1);				/* FIFO bit */
#else
	writel(0x51, (void __iomem *)(DDR_PHY_BASE + 0x04));	/* MEM_CFG | bit6 FIFO */
#endif
	phy_set_range(0xa, 1, 3, 3);				/* FIFO depth */
	phy_set_range(0x8, 0, 2, 3);				/* TX write pointer adj */

	/* Vendor T40N per-bit DQ/DQS skew defaults. */
#if !defined(CONFIG_T40_DDR3)
	ddr_innophy_set_skew_t40n();
#endif
}
