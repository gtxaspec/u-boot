// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 DDR: Synopsys uMCTL2 controller + Innophy PHY init.
 *
 * UCLASS_RAM driver, sibling of ddr_t31.c (T32 is XBurst1 but pairs a
 * Synopsys uMCTL2-class controller with an Innophy training PHY - a
 * completely different IP from the legacy XBurst1 DDRC) and of the
 * XBurst2 ddr_innophy.c. Faithful transliteration of the vendor
 * known-good path (U-Boot 2022.10 arch/mips/mach-xburst/PRJ/
 * ddr_innophy.c), formerly arch/mips/mach-xburst/t32/sdram.c. The
 * per-SKU register/clock values come from the DT-selected
 * struct ingenic_t32_ddr_params (of_match .data) instead of
 * compile-time CONFIG_T32_VARIANT_* #if branches; the old
 * CONFIG_T32_DDR3/LPDDR3/HWTRAIN build branches are runtime
 * cfg->type tests (DDR2 = soft-train, DDR3/LPDDR3 = hardware-train).
 *
 * Probes off DT in both SPL (DDR bring-up) and U-Boot proper (just
 * records DRAM size for ram_get_info()). The register write order,
 * poll loops and delays are timing-critical and reproduced exactly
 * from the vendor source. Still intentionally NOT ported
 * (upstream-friendly, no host tool / no generated header): the
 * eFUSE-KGD hamming machinery - the vendor init_ddr_par[] per-type
 * defaults apply (kgd_rtt_dic comes out fixed per type).
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
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
#include <linux/types.h>
#include <asm/io.h>
#include <asm/global_data.h>
#include <mach/t32.h>
#include "ddr_t32.h"

DECLARE_GLOBAL_DATA_PTR;

#define T32_EXTAL_HZ	24000000U

/*
 * --- drive-strength / ODT parameters ---
 * Indices into the par array (vendor enum order). No eFUSE-KGD in this
 * port, so the vendor init_ddr_par[] per-type defaults apply
 * (efuse_ddr_get.c L339-359); KGD_RTT_DIC resolves per type. Only
 * indices 0..7 (ddrp_set_drv_odt) and KGD_RTT_DIC (ddrc_init) are
 * consumed.
 */
enum {
	T32_ODT_PD, T32_ODT_PU, T32_CMD_RC_PD, T32_CMD_RC_PU,
	T32_CLK_RC_PD, T32_CLK_RC_PU, T32_DQX_RC_PD, T32_DQX_RC_PU,
	T32_VREF, T32_KGD_ODT, T32_KGD_DS, T32_KGD_RTT_DIC,
	T32_DDR_PAR_NUM,
};

/* DDR2 init_ddr_par[] defaults (no eFUSE). */
static const u32 t32_ddr_par_ddr2[T32_DDR_PAR_NUM] = {
	[T32_ODT_PD] = 0x00, [T32_ODT_PU] = 0x00,
	[T32_CMD_RC_PD] = 0x08, [T32_CMD_RC_PU] = 0x08,
	[T32_CLK_RC_PD] = 0x08, [T32_CLK_RC_PU] = 0x08,
	[T32_DQX_RC_PD] = 0x08, [T32_DQX_RC_PU] = 0x08,
	[T32_VREF] = 0x70, [T32_KGD_ODT] = 0x00,
	[T32_KGD_DS] = 0x00, [T32_KGD_RTT_DIC] = 0x00,
};

/* DDR3 init_ddr_par[] defaults (no eFUSE). */
static const u32 t32_ddr_par_ddr3[T32_DDR_PAR_NUM] = {
	[T32_ODT_PD] = 0x00, [T32_ODT_PU] = 0x00,
	[T32_CMD_RC_PD] = 0x0e, [T32_CMD_RC_PU] = 0x0e,
	[T32_CLK_RC_PD] = 0x0e, [T32_CLK_RC_PU] = 0x0e,
	[T32_DQX_RC_PD] = 0x0e, [T32_DQX_RC_PU] = 0x0e,
	[T32_VREF] = 0x70, [T32_KGD_ODT] = 0x00,
	[T32_KGD_DS] = 0x00, [T32_KGD_RTT_DIC] = 0x00,
};

/* LPDDR3 init_ddr_par[] defaults (no eFUSE). */
static const u32 t32_ddr_par_lpddr3[T32_DDR_PAR_NUM] = {
	[T32_ODT_PD] = 0x00, [T32_ODT_PU] = 0x00,
	[T32_CMD_RC_PD] = 0x0e, [T32_CMD_RC_PU] = 0x0e,
	[T32_CLK_RC_PD] = 0x0e, [T32_CLK_RC_PU] = 0x0e,
	[T32_DQX_RC_PD] = 0x0e, [T32_DQX_RC_PU] = 0x0e,
	[T32_VREF] = 0x70, [T32_KGD_ODT] = 0x00,
	[T32_KGD_DS] = 0x02, [T32_KGD_RTT_DIC] = 0x02,
};

static const u32 *t32_ddr_par(const struct ingenic_t32_ddr_params *cfg)
{
	switch (cfg->type) {
	case T32_DDR_TYPE_LPDDR3:
		return t32_ddr_par_lpddr3;
	case T32_DDR_TYPE_DDR3:
		return t32_ddr_par_ddr3;
	default:
		return t32_ddr_par_ddr2;
	}
}

static u32 cpm_readl(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void cpm_writel(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

/*
 * Errata: clear CP0 $9 sel4 bit 1 so the CPU may issue speculative
 * reads to DDR (vendor spl.c enable_cpu_read_ddr()).
 */
static void enable_cpu_read_ddr(void)
{
	int res = 0, res1 = 0;

	__asm__ __volatile__(
		".set	push		\n"
		".set	reorder		\n"
		"mfc0	%0,$9,4		\n"
		"li	%1,-3		\n"
		"and	%0,%0,%1	\n"
		"mtc0	%0,$9,4		\n"
		".set	pop		\n"
		: "=r"(res), "=r"(res1));
}

/* CPMPCR -> Hz (M/N/OD0/OD1 form; same decode as t32_pll_rate). */
static u32 t32_mpll_rate(void)
{
	u32 v = cpm_readl(CPM_CPMPCR);
	u32 m = (v >> 20) & 0xfff;
	u32 n = (v >> 14) & 0x3f;
	u32 od1 = (v >> 11) & 0x7;
	u32 od0 = (v >> 8) & 0x7;

	if (!n)
		n = 1;
	if (!od1)
		od1 = 1;
	if (!od0)
		od0 = 1;

	return (u32)((u64)T32_EXTAL_HZ * m / n / od1 / od0);
}

/*
 * DDR clock. Vendor sdram_init() does _clk_set_rate(DDR,
 * CONFIG_SYS_MEM_FREQ / 2): the MPLL-sourced DDR CK (cfg->ddr_ck_hz =
 * data rate / 2). CPM_DDRCDR layout (vendor CGU descriptor [DDR] =
 * {.., sel_bit 30, ce 29, busy 28, stop 27}): src[31:30] (1=APLL,
 * 2=MPLL), ce[29], busy[28], stop[27], divider[7:0].
 * cdr = ceil(MPLL / CK) - 1.
 */
static void ddr_clk_init(const struct ingenic_t32_ddr_params *cfg)
{
	u32 mpll = t32_mpll_rate();
	u32 cdr = ((mpll + cfg->ddr_ck_hz - 1) / cfg->ddr_ck_hz - 1) & 0xff;
	u32 v;

	/*
	 * Ungate the DDR controller clock (CPM_CLKGR0 bit 27) first.
	 * Without this the uMCTL2 controller has no APB/AXI clock, stays
	 * in init state forever, and the PHY-training STAT poll spins.
	 * Vendor T32 clk.c clk_init() clears the same bit before any DDR
	 * access.
	 */
	cpm_writel(cpm_readl(CPM_CLKGR0) & ~BIT(27), CPM_CLKGR0);

	v = cpm_readl(CPM_DDRCDR);
	v &= ~((3u << 30) | (1u << 28) | (1u << 27) | 0xff);
	v |= (2u << 30) | (1u << 29) | cdr;	/* src=MPLL, ce, divider */
	cpm_writel(v, CPM_DDRCDR);
	while (cpm_readl(CPM_DDRCDR) & (1u << 28))	/* wait change-busy clear */
		;
}

/* ------------------------------------------------------------------
 * DDR2 soft-training (vendor DDR_SOFT_TRAIN path)
 * ------------------------------------------------------------------ */

/* Small uncached pattern check used to score soft-training taps. */
static int ddr_mem_pattern(void)
{
	volatile u32 tmp;
	u32 td;

	for (tmp = 0xa0000000; tmp < 0xa0000020; tmp += 4) {
		td = tmp;
		*(volatile u32 *)tmp = td;
		if (*(volatile u32 *)tmp != td)
			return -1;
	}
	return 0;
}

static void ddrp_training_read_calib_bypass(void)
{
	SET_INNOPHY_REG(reg_train_reg_update_en, 0x1);

	SET_INNOPHY_REG(reg_a_l_rxmen0_delay_bp,
			READ_INNOPHY_REG(reg_a_l_cycsel));
	SET_INNOPHY_REG(reg_a_l_rxmen0_ophsel_bp,
			READ_INNOPHY_REG(reg_a_l_ophsel));
	SET_INNOPHY_REG(reg_a_l_rxmen0_sdlltap_bp,
			READ_INNOPHY_REG(reg_a_l_dllsel));
	SET_INNOPHY_REG(reg_a_h_rxmen0_delay_bp,
			READ_INNOPHY_REG(reg_a_h_cycsel));
	SET_INNOPHY_REG(reg_a_h_rxmen0_ophsel_bp,
			READ_INNOPHY_REG(reg_a_h_ophsel));
	SET_INNOPHY_REG(reg_a_h_rxmen0_sdlltap_bp,
			READ_INNOPHY_REG(reg_a_h_dllsel));

	SET_INNOPHY_REG(reg_calib_freq_update, 0x1);
	udelay(1);
	SET_INNOPHY_REG(reg_calib_freq_update, 0x0);
	SET_INNOPHY_REG(reg_calib_bypass, 0x1);
	SET_INNOPHY_REG(reg_train_reg_update_en, 0x0);
}

static void ddrp_training_read_train_bypass(u32 dqs0, u32 dqs1, u32 dq)
{
	SET_INNOPHY_REG(reg_train_reg_update_en, 0x1);

	SET_INNOPHY_REG(reg_a_l_cs0_dqs_invdelayselrx, dqs0);
	SET_INNOPHY_REG(reg_a_l_cs0_dqsb_invdelayselrx, dqs0);
	SET_INNOPHY_REG(reg_a_h_cs0_dqs_invdelayselrx, dqs1);
	SET_INNOPHY_REG(reg_a_h_cs0_dqsb_invdelayselrx, dqs1);

	SET_INNOPHY_REG(reg_a_l_cs0_dm_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq0_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq1_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq2_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq3_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq4_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq5_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq6_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq7_invdelayselrx, dq);

	SET_INNOPHY_REG(reg_a_h_cs0_dm_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq0_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq1_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq2_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq3_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq4_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq5_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq6_invdelayselrx, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq7_invdelayselrx, dq);

	SET_INNOPHY_REG(reg_rd_train_freq_update, 0x1);
	udelay(1);
	SET_INNOPHY_REG(reg_rd_train_freq_update, 0x0);
	SET_INNOPHY_REG(reg_train_reg_update_en, 0x0);
}

static void ddrp_training_write_train_bypass_dq(u32 dq)
{
	SET_INNOPHY_REG(reg_train_reg_update_en, 1);

	SET_INNOPHY_REG(reg_a_l_cs0_dm_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq0_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq1_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq2_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq3_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq4_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq5_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq6_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_l_cs0_dq7_invdelaysel, dq);

	SET_INNOPHY_REG(reg_a_h_cs0_dm_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq0_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq1_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq2_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq3_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq4_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq5_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq6_invdelaysel, dq);
	SET_INNOPHY_REG(reg_a_h_cs0_dq7_invdelaysel, dq);

	SET_INNOPHY_REG(reg_wl_freq_update, 1);
	udelay(1);
	SET_INNOPHY_REG(reg_wl_freq_update, 0);
	SET_INNOPHY_REG(reg_train_reg_update_en, 0);
}

static void ddrp_training_soft_read_train(u32 dq)
{
	u32 min = 0xff, med, width = 0;
	u32 dqs;

	ddrp_training_read_train_bypass(0, 0, dq);

	for (dqs = 0; dqs <= 0x7f; dqs++) {
		SET_INNOPHY_REG(reg_train_reg_update_en, 1);
		SET_INNOPHY_REG(reg_a_l_cs0_dqs_invdelayselrx, dqs);
		SET_INNOPHY_REG(reg_a_l_cs0_dqsb_invdelayselrx, dqs);
		SET_INNOPHY_REG(reg_a_h_cs0_dqs_invdelayselrx, dqs);
		SET_INNOPHY_REG(reg_a_h_cs0_dqsb_invdelayselrx, dqs);
		SET_INNOPHY_REG(reg_rd_train_freq_update, 1);
		udelay(1);
		SET_INNOPHY_REG(reg_rd_train_freq_update, 0);
		SET_INNOPHY_REG(reg_train_reg_update_en, 0);

		if (!ddr_mem_pattern()) {
			width++;
			if (min == 0xff)
				min = dqs;
		}
	}
	if (min == 0xff)
		min = 0;
	med = min + width / 2;

	SET_INNOPHY_REG(reg_train_reg_update_en, 1);
	SET_INNOPHY_REG(reg_a_l_cs0_dqs_invdelayselrx, med);
	SET_INNOPHY_REG(reg_a_l_cs0_dqsb_invdelayselrx, med);
	SET_INNOPHY_REG(reg_a_h_cs0_dqs_invdelayselrx, med);
	SET_INNOPHY_REG(reg_a_h_cs0_dqsb_invdelayselrx, med);
	SET_INNOPHY_REG(reg_rd_train_freq_update, 1);
	udelay(1);
	SET_INNOPHY_REG(reg_rd_train_freq_update, 0);
	SET_INNOPHY_REG(reg_train_reg_update_en, 0);
}

static void ddrp_training_soft_write_train(void)
{
	u32 min = 0xff, med, width = 0;
	u32 dqs;

	SET_INNOPHY_REG(reg_wl_bypass, 1);
	for (dqs = 0; dqs <= 0xff; dqs++) {
		SET_INNOPHY_REG(reg_train_reg_update_en, 1);
		SET_INNOPHY_REG(reg_a_l_cs0_dqs_invdelaysel, dqs);
		SET_INNOPHY_REG(reg_a_l_cs0_dqsb_invdelaysel, dqs);
		SET_INNOPHY_REG(reg_a_h_cs0_dqs_invdelaysel, dqs);
		SET_INNOPHY_REG(reg_a_h_cs0_dqsb_invdelaysel, dqs);
		SET_INNOPHY_REG(reg_wl_freq_update, 1);
		udelay(1);
		SET_INNOPHY_REG(reg_wl_freq_update, 0);
		SET_INNOPHY_REG(reg_train_reg_update_en, 0);

		if (!ddr_mem_pattern()) {
			width++;
			if (min == 0xff)
				min = dqs;
		}
	}
	if (min == 0xff)
		min = 0;
	med = min + width / 2;

	SET_INNOPHY_REG(reg_train_reg_update_en, 1);
	SET_INNOPHY_REG(reg_a_l_cs0_dqs_invdelaysel, med);
	SET_INNOPHY_REG(reg_a_l_cs0_dqsb_invdelaysel, med);
	SET_INNOPHY_REG(reg_a_h_cs0_dqs_invdelaysel, med);
	SET_INNOPHY_REG(reg_a_h_cs0_dqsb_invdelaysel, med);
	SET_INNOPHY_REG(reg_wl_freq_update, 1);
	udelay(1);
	SET_INNOPHY_REG(reg_wl_freq_update, 0);
	SET_INNOPHY_REG(reg_train_reg_update_en, 0);
}

/* DQS gating calibration (rank 0) - shared by both training paths. */
static void ddrp_dqs_calibration(void)
{
	SET_INNOPHY_REG(reg_phy_refresh_en, 0x1);
	SET_INNOPHY_REG(reg_calcs_sel, 0x2);
	SET_INNOPHY_REG(reg_start_calib, 0x1);

	BNE_INNOPHY_REG(calib_end, 0x1);
	BNE_INNOPHY_REG(calib_done_byte, 0x3);

	SET_INNOPHY_REG(reg_phy_refresh_en, 0x0);
	SET_INNOPHY_REG(reg_start_calib, 0x0);
	SET_INNOPHY_REG(reg_calcs_sel, 0x0);
}

/* ------------------------------------------------------------------
 * DDR3/LPDDR3 hardware training (vendor DDR_HARDWARE_TRAIN path;
 * PRINT_DDRP/dwc_debug stripped as in the DDR2 soft port). read/write
 * training are type-independent; write-leveling differs DDR3 vs
 * LPDDR3.
 * ------------------------------------------------------------------ */

static void ddrp_training_write_leveling(const struct ingenic_t32_ddr_params *cfg)
{
	SET_INNOPHY_REG(reg_wlcs_sel, 0x2);
	if (cfg->type == T32_DDR_TYPE_LPDDR3)
		/* LPDDR3: bits[7:0] = MR2[7:0] (INIT3), bits[15:8] = 0. */
		SET_INNOPHY_REG(reg_wl_loadmode, cfg->init3 & 0xff);
	else
		/* DDR3: bits[13:0] = MR1[13:0] (INIT3), bits[15:14] = 2'b01. */
		SET_INNOPHY_REG(reg_wl_loadmode,
				(cfg->init3 & 0x3fff) | (1 << 14));
	SET_INNOPHY_REG(reg_phy_refresh_en, 0x1);
	SET_INNOPHY_REG(reg_wl_enable, 0x1);
	BNE_INNOPHY_REG(wl_done_byte, 0x3);
	BNE_INNOPHY_REG(reg_wl_end, 0x1);
	SET_INNOPHY_REG(reg_wl_enable, 0x0);
	SET_INNOPHY_REG(reg_wlcs_sel, 0x0);
}

static void ddrp_training_read_training(void)
{
	SET_INNOPHY_REG(reg_phy_refresh_en, 0x1);
	SET_INNOPHY_REG(reg_rdtrain_cs_sel, 0x2);
	SET_INNOPHY_REG(reg_a_l_rd_train_dqs_default, 0x1f);
	SET_INNOPHY_REG(reg_a_h_rd_train_dqs_default, 0x1f);
	SET_INNOPHY_REG(reg_dq_rd_train_en, 0x1);
	BNE_INNOPHY_REG(train_true_done, 0x1);
	SET_INNOPHY_REG(reg_dq_rd_train_en, 0x0);
	SET_INNOPHY_REG(reg_rd_train_dqs_range_bypass, 0x0);
	SET_INNOPHY_REG(reg_rdtrain_cs_sel, 0x0);
	SET_INNOPHY_REG(reg_phy_refresh_en, 0x0);
}

static void ddrp_training_write_training(void)
{
	SET_INNOPHY_REG(reg_wrtrain_check_data_value_random_gen, 0x1);
	SET_INNOPHY_REG(reg_wrtrain_cs_sel, 0x2);
	SET_INNOPHY_REG(reg_phy_refresh_en, 0x1);
	SET_INNOPHY_REG(reg_wr_train_dqs_default_bypass, 0x0);
	SET_INNOPHY_REG(reg_dq_wr_train_auto, 0x1);
	SET_INNOPHY_REG(reg_dq_wr_train_en, 0x1);
	BNE_INNOPHY_REG(train_step1_delay_done, 0x1);
	BNE_INNOPHY_REG(train_all_step_done, 0x1);
	SET_INNOPHY_REG(reg_dq_wr_train_en, 0x0);
	SET_INNOPHY_REG(reg_dq_wr_train_auto, 0x0);	/* must clear post-train */
	SET_INNOPHY_REG(reg_wr_train_dqs_default_bypass, 0x0);
	SET_INNOPHY_REG(reg_wrtrain_cs_sel, 0x0);
	SET_INNOPHY_REG(reg_phy_refresh_en, 0x0);
}

static void ddrp_training(const struct ingenic_t32_ddr_params *cfg)
{
	u32 stat;

	ddrc_writel(PCTRL0, 0x1);
	ddrc_writel(PCTRL1, 0x1);
	ddrc_writel(PCTRL2, 0x1);
	ddrc_writel(PCTRL3, 0x1);
	ddrc_writel(PCTRL4, 0x1);
	ddrc_writel(PCTRL5, 0x1);
	ddrc_writel(PCTRL6, 0x1);

	SET_INNOPHY_REG(reg_dq_wr_train_auto, 0x0);

	do {
		stat = ddrc_readl(STAT);
	} while (!(stat & 0x1));

	if (cfg->type == T32_DDR_TYPE_DDR2) {
		ddrp_dqs_calibration();
		ddrp_training_read_calib_bypass();
		ddrp_training_soft_read_train(0x40);
		ddrp_training_write_train_bypass_dq(0x80);
		ddrp_training_soft_write_train();
	} else {
		ddrp_training_write_leveling(cfg);
		ddrp_dqs_calibration();
		ddrp_training_read_training();
		ddrp_training_write_training();
	}
}

static void ddrc_init(const struct ingenic_t32_ddr_params *cfg,
		      u32 kgd_rtt_dic)
{
	u32 data;

	ddrc_writel(MSTR, cfg->mstr);

	ddrc_writel(INIT0, cfg->init0);
	ddrc_writel(INIT1, cfg->init1);
	/* INIT2 is LPDDR2-only (skipped). INIT3/INIT4 masks + INIT5 are
	 * type-specific (vendor ddrc_init). */
	switch (cfg->type) {
	case T32_DDR_TYPE_LPDDR3:
		ddrc_writel(INIT3, cfg->init3);
		ddrc_writel(INIT4, (cfg->init4 & ~0xf0000) |
				   (kgd_rtt_dic << 16));
		ddrc_writel(INIT5, cfg->init5);
		break;
	case T32_DDR_TYPE_DDR3:
		ddrc_writel(INIT3, (cfg->init3 & ~0x266) | kgd_rtt_dic);
		ddrc_writel(INIT4, cfg->init4);
		ddrc_writel(INIT5, cfg->init5);
		break;
	default:	/* DDR2 */
		ddrc_writel(INIT3, (cfg->init3 & ~0x46) | kgd_rtt_dic);
		ddrc_writel(INIT4, cfg->init4);
		break;
	}

	ddrc_writel(ADDRMAP1, cfg->addrmap1);
	ddrc_writel(ADDRMAP2, cfg->addrmap2);
	ddrc_writel(ADDRMAP3, cfg->addrmap3);
	ddrc_writel(ADDRMAP4, cfg->addrmap4);
	ddrc_writel(ADDRMAP5, cfg->addrmap5);
	ddrc_writel(ADDRMAP6, cfg->addrmap6);

	ddrc_writel(DRAMTMG0, cfg->timing0);
	ddrc_writel(DRAMTMG1, cfg->timing1);
	ddrc_writel(DRAMTMG2, cfg->timing2);
	ddrc_writel(DRAMTMG3, cfg->timing3);
	ddrc_writel(DRAMTMG4, cfg->timing4);
	ddrc_writel(DRAMTMG5, cfg->timing5);
	ddrc_writel(DRAMTMG7, cfg->timing7);
	if (cfg->type == T32_DDR_TYPE_LPDDR3) {
		ddrc_writel(DRAMTMG6, cfg->timing6);	/* LPDDR2/3 */
		ddrc_writel(DRAMTMG14, cfg->timing14);
	} else {
		ddrc_writel(DRAMTMG8, cfg->timing8);	/* DDR2/DDR3 */
	}

	ddrc_writel(DFITMG0, cfg->dfitmg0);
	ddrc_writel(DFITMG1, cfg->dfitmg1);
	ddrc_writel(DFIUPD0, cfg->dfiupd0);

	ddrc_writel(RFSHTMG, cfg->rfshtmg);
	ddrc_writel(RFSHCTL3, cfg->rfshctl3);

	if (cfg->type == T32_DDR_TYPE_DDR2)
		ddrc_writel(ODTCFG, cfg->odtcfg);	/* DDR2/LPDDR2 only */

	data = ddrc_readl(SCHED);
	data &= ~(1 << 2);
	ddrc_writel(SCHED, data);
}

/*
 * Innophy bring-up. The pin-wrap table (vendor ddrp_map_set) is NOT
 * programmed: vendor PRJ/ddr_innophy.c skips it when SoCID = PRJ007
 * (T32); only PRJ008 (T33) wraps DDR pins through the Innophy pin-swap
 * registers. Programming the swap table on T32 silicon mis-wires
 * CMD/DQ from the DRAM's POV and the DQS gating calibration
 * BNE_INNOPHY_REG(calib_end, 0x1) hangs.
 */
static void ddrp_init(const struct ingenic_t32_ddr_params *cfg,
		      u32 ddrc_reset)
{
	u32 data;

	SET_INNOPHY_REG(reg_train_reg_update_en, 0x0);
	SET_INNOPHY_REG(reg_channel_en, cfg->phy_channel_en);
	SET_INNOPHY_REG(CWL_FRE_OP0, cfg->phy_cwl);
	SET_INNOPHY_REG(CL_FRE_OP0, cfg->phy_cl);
	SET_INNOPHY_REG(AL_FRE_OP0, cfg->phy_al);
	SET_INNOPHY_REG(reg_phy_trfc, cfg->phy_trfc);
	SET_INNOPHY_REG(reg_phy_trefi, cfg->phy_trefi);
	SET_INNOPHY_REG(mem_select_t, cfg->phy_mem_sel);

	SET_INNOPHY_REG(reg_pllpostdiven_fsp0, cfg->phy_pllpostdiven);
	SET_INNOPHY_REG(reg_pllpostdiven_fsp1, cfg->phy_pllpostdiven);
	SET_INNOPHY_REG(reg_pllpostdiven_fsp2, cfg->phy_pllpostdiven);
	SET_INNOPHY_REG(reg_pllpostdiven_fsp3, cfg->phy_pllpostdiven);
	SET_INNOPHY_REG(reg_pllpostdiv_fsp0, cfg->phy_pllpostdiv);
	SET_INNOPHY_REG(reg_pllpostdiv_fsp1, cfg->phy_pllpostdiv);
	SET_INNOPHY_REG(reg_pllpostdiv_fsp2, cfg->phy_pllpostdiv);
	SET_INNOPHY_REG(reg_pllpostdiv_fsp3, cfg->phy_pllpostdiv);

	SET_INNOPHY_REG(reg_pllcpp_bias_dqcmd, 0x4);
	SET_INNOPHY_REG(reg_pllcpi_bias_fsp0, 0x5);
	SET_INNOPHY_REG(reg_pllcpi_bias_fsp1, 0x5);
	SET_INNOPHY_REG(reg_pllcpi_bias_fsp2, 0x5);
	SET_INNOPHY_REG(reg_pllcpi_bias_fsp3, 0x5);

	ddrc_reset &= ~0x10;	/* ddrc reset ok */
	cpm_writel(ddrc_reset, CPM_SRBC0);
	udelay(500);

	data = ddrc_readl(DFIMISC);
	data |= 0x20;
	ddrc_writel(DFIMISC, data);

	while (!(ddrc_readl(DFISTAT) & 0x1))
		;

	data = ddrc_readl(DFIMISC);
	data &= ~(1 << 5);
	ddrc_writel(DFIMISC, data);
}

static void ddrp_set_drv_odt(const u32 *p)
{
	SET_INNOPHY_REG(reg_a_l_abutodtpudq_reg, p[T32_ODT_PU]);
	SET_INNOPHY_REG(reg_a_h_abutodtpudq_reg, p[T32_ODT_PU]);
	SET_INNOPHY_REG(reg_a_l_abutodtpddq_reg, p[T32_ODT_PD]);
	SET_INNOPHY_REG(reg_a_h_abutodtpddq_reg, p[T32_ODT_PD]);
	SET_INNOPHY_REG(reg_cmd_abutprcomp_reg, p[T32_CMD_RC_PU]);
	SET_INNOPHY_REG(reg_cmd_abutnrcomp_reg, p[T32_CMD_RC_PD]);
	SET_INNOPHY_REG(reg_cmd_abutprcomp_ck0_reg, p[T32_CLK_RC_PU]);
	SET_INNOPHY_REG(reg_cmd_abutnrcomp_ck0_reg, p[T32_CLK_RC_PD]);
	SET_INNOPHY_REG(reg_a_l_abutprcompdq_reg, p[T32_DQX_RC_PU]);
	SET_INNOPHY_REG(reg_a_h_abutprcompdq_reg, p[T32_DQX_RC_PU]);
	SET_INNOPHY_REG(reg_a_l_abutnrcompdq_reg, p[T32_DQX_RC_PD]);
	SET_INNOPHY_REG(reg_a_h_abutnrcompdq_reg, p[T32_DQX_RC_PD]);
}

static void ddrc_set_port_priority(void)
{
	u32 port, val;

	/* ISP port (0) high priority */
	port = 0;
	val = ddrc_readl(PCFGR0 + 0xb0 * port);
	val |= (0 << 13) | (1 << 12);
	val &= ~0x3ff;
	ddrc_writel(PCFGR0 + 0xb0 * port, val);

	val = ddrc_readl(PCFGW0 + 0xb0 * port);
	val |= (0 << 13) | (1 << 12);
	val &= ~0x3ff;
	ddrc_writel(PCFGW0 + 0xb0 * port, val);

	/* CPU port (6) high priority */
	port = 6;
	val = ddrc_readl(PCFGR0 + 0xb0 * port);
	val |= (0 << 13) | (1 << 12);
	val &= ~0x3ff;
	ddrc_writel(PCFGR0 + 0xb0 * port, val);

	val = ddrc_readl(PCFGW0 + 0xb0 * port);
	val |= (0 << 13) | (1 << 12);
	val &= ~0x3ff;
	ddrc_writel(PCFGW0 + 0xb0 * port, val);

	/* VPU port (4) high priority */
	port = 4;
	val = 0x500d;
	ddrc_writel(PCFGR0 + 0xb0 * port, val);
	ddrc_writel(PCFGW0 + 0xb0 * port, val);

	/* map_region: 13~15 HPR; 0~12 LPR */
	for (port = 0; port < 7; port++) {
		if (port == 5)
			continue;
		val = ddrc_readl(PCFGQOS00 + 0xb0 * port);
		val |= (2 << 20) | (0 << 16) | 12;
		ddrc_writel(PCFGQOS00 + 0xb0 * port, val);
	}

	/* ar/w qos: CPU 15; ISP 14; VPU 13 */
	writel(0xee, (void __iomem *)(DDR_QOS_BASE + 0x30));
	writel(0xffdd, (void __iomem *)(DDR_QOS_BASE + 0x70));
}

static void ddrc_dfi_init(const struct ingenic_t32_ddr_params *cfg)
{
	const u32 *p = t32_ddr_par(cfg);
	u32 ddrc_reset;

	/* isolate ISP/VPU/IVDC/TNPU from the DDR domain during init */
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x20, 19, 19, 0);	/* ISP */
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x20, 28, 28, 0);	/* VPU */
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x28, 3, 3, 0);		/* IVDC */
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x28, 11, 11, 0);	/* TNPU */

	/* ddrc & ddrp soft reset (bit3/4 apb reset; bit4 ddrc reset) */
	ddrc_reset = cpm_readl(CPM_SRBC0);
	ddrc_reset |= 0x18;
	cpm_writel(ddrc_reset, CPM_SRBC0);
	udelay(500);
	ddrc_reset &= ~0x8;	/* apb reset ok */
	cpm_writel(ddrc_reset, CPM_SRBC0);
	udelay(500);

	ddrc_writel(SWCTLSTATIC, 0x1);
	ddrc_writel(SWCTL, 0x0);

	ddrc_init(cfg, p[T32_KGD_RTT_DIC]);
	ddrp_init(cfg, ddrc_reset);
	ddrp_set_drv_odt(p);
	ddrp_training(cfg);

	/* re-attach ISP/VPU/IVDC/TNPU */
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x20, 19, 19, 1);
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x20, 28, 28, 1);
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x28, 3, 3, 1);
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x28, 11, 11, 1);

	ddrc_set_port_priority();
	ddrc_writel(SWCTLSTATIC, 0x0);
	ddrc_writel(SWCTL, 0x1);
}

/* Top-level DDR2/DDR3/LPDDR3 init (vendor sdram_init() uMCTL2 path). */
int ingenic_t32_ddr_sdram_init(const struct ingenic_t32_ddr_params *cfg)
{
	enable_cpu_read_ddr();
	ddr_clk_init(cfg);
	ddrc_dfi_init(cfg);

	return 0;
}

/*
 * DT compatible for the T32 DDR node - one binding for every T32 SKU; the
 * board leaf .dts supplies the per-SKU "ingenic,sdram-params" u32 array.
 * Shared by the pre-DM FDT read below and the driver of_match - no per-SKU
 * compatible, no of_match .data variant table.
 */
#define INGENIC_T32_DDR_COMPATIBLE	"ingenic,t32-ddr-innophy"

/*
 * Read the per-SKU params from the &ddr node's "ingenic,sdram-params" array in
 * the live FDT, before driver model is up. The struct is all-u32 and the
 * property order IS the field order, so it deserializes in one shot.
 */
static int ddr_params_from_fdt(struct ingenic_t32_ddr_params *out)
{
	const void *blob = gd->fdt_blob;
	int node;

	if (!blob)
		return -ENODEV;

	node = fdt_node_offset_by_compatible(blob, -1,
					     INGENIC_T32_DDR_COMPATIBLE);
	if (node < 0)
		return -ENODEV;

	return fdtdec_get_int_array(blob, node, "ingenic,sdram-params",
				    (u32 *)out, sizeof(*out) / sizeof(u32));
}

/*
 * SPL helper for t32/pll.c: hand back the SKU's PLL/CPCCR setpoints from the
 * DDR node's ingenic,sdram-params array. Runs before driver model is up.
 */
int ingenic_t32_ddr_pll_setpoints(u32 *cpapcr, u32 *cpmpcr,
				  u32 *cpccr_div, u32 *cpccr_sel)
{
	struct ingenic_t32_ddr_params p;
	int ret;

	ret = ddr_params_from_fdt(&p);
	if (ret)
		return ret;

	*cpapcr = p.cpapcr;
	*cpmpcr = p.cpmpcr;
	*cpccr_div = p.cpccr_div;
	*cpccr_sel = p.cpccr_sel;
	return 0;
}

/* ------------------------------------------------------------------
 * UCLASS_RAM driver. The per-SKU params come from the &ddr node's
 * "ingenic,sdram-params" array, read into platdata by of_to_plat (the
 * mainline rk3328 DMC shape). The SPL probe brings DRAM up; the
 * U-Boot-proper probe just records the size (DRAM is already alive). PLL is
 * programmed earlier, in t32/pll.c, via ingenic_t32_ddr_pll_setpoints().
 * ------------------------------------------------------------------ */

struct ingenic_t32_ddr_plat {
#if CONFIG_IS_ENABLED(OF_PLATDATA)
	struct dtd_ingenic_t32_ddr_innophy dtplat;
#else
	struct ingenic_t32_ddr_params params;
#endif
};

static int ingenic_t32_ddr_of_to_plat(struct udevice *dev)
{
#if CONFIG_IS_ENABLED(OF_REAL)
	struct ingenic_t32_ddr_plat *plat = dev_get_plat(dev);
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

static int ingenic_t32_ddr_probe(struct udevice *dev)
{
	struct ingenic_t32_ddr_priv *p = dev_get_priv(dev);
	struct ingenic_t32_ddr_plat *plat = dev_get_plat(dev);
#if CONFIG_IS_ENABLED(OF_PLATDATA)
	const struct ingenic_t32_ddr_params *params =
		(const struct ingenic_t32_ddr_params *)
			plat->dtplat.ingenic_sdram_params;
#else
	const struct ingenic_t32_ddr_params *params = &plat->params;
#endif

	p->ram_size = params->size;

	if (IS_ENABLED(CONFIG_XPL_BUILD))
		return ingenic_t32_ddr_sdram_init(params);

	return 0;
}

static int ingenic_t32_ddr_get_info(struct udevice *dev, struct ram_info *info)
{
	struct ingenic_t32_ddr_priv *p = dev_get_priv(dev);

	info->base = 0;
	info->size = p->ram_size;
	return 0;
}

static const struct ram_ops ingenic_t32_ddr_ops = {
	.get_info = ingenic_t32_ddr_get_info,
};

static const struct udevice_id ingenic_t32_ddr_ids[] = {
	{ .compatible = INGENIC_T32_DDR_COMPATIBLE },
	{ }
};

/*
 * Name the driver after its compatible ("ingenic,t32-ddr-innophy" ->
 * "ingenic_t32_ddr_innophy") so an OF_PLATDATA (dtoc) build would bind it by
 * the compatible-derived name with a matching dtd_ingenic_t32_ddr_innophy
 * platdata struct. T32 is OF_CONTROL today; this keeps it consistent with
 * ingenic_t31_ddr_innophy and TPL-ready.
 */
U_BOOT_DRIVER(ingenic_t32_ddr_innophy) = {
	.name		= "ingenic_t32_ddr_innophy",
	.id		= UCLASS_RAM,
	.of_match	= ingenic_t32_ddr_ids,
	.of_to_plat	= ingenic_t32_ddr_of_to_plat,
	.ops		= &ingenic_t32_ddr_ops,
	.probe		= ingenic_t32_ddr_probe,
	.priv_auto	= sizeof(struct ingenic_t32_ddr_priv),
	.plat_auto	= sizeof(struct ingenic_t32_ddr_plat),
};
