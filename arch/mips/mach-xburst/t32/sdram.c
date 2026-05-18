// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 DDR2: Synopsys uMCTL2 controller + Innophy PHY init (SPL)
 *
 * Faithful transliteration of the vendor known-good DDR2 path
 * (U-Boot 2022.10 arch/mips/mach-xburst/PRJ/ddr_innophy.c) for the
 * T32 (PRJ007) profile: DDR2 M14D5121632A, 64 MB, 16-bit bus, CS0
 * only, controller 650 MHz (tCK 1538 ps), Innophy "soft training".
 *
 * The register write order, poll loops and delays are timing
 * critical and reproduced exactly from the vendor source. Removed
 * for this single-chip mainline profile: LPDDR2/3 + DDR3 paths,
 * hardware training, the eFUSE-KGD hamming machinery (replaced by
 * the fixed DDR2 ddr_par_init() defaults) and the multi-chip match
 * table (one static const parameter set, no global_reg_value).
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <mach/t32.h>
#include <mach/t32-ddr.h>

/*
 * Vendor get_ddr_par() collapses to these DDR2 defaults when no
 * eFUSE-KGD is programmed (this port). KGD_RTT_DIC computes to 0
 * for DDR2 (kgd_rtt = kgd_dic = 0).
 */
/* DDR clock (CPM_DDRCDR <- MPLL/divider); implemented in t32/pll.c. */
void ddr_clk_init(void);

static const u32 t32_ddr_par[T32_DDR_PAR_NUM] = T32_DDR2_DRV_ODT_DEFAULTS;

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

static void ddrp_dqs_calibration(void)
{
	/* dqs gating calibration (rank 0) */
	SET_INNOPHY_REG(reg_phy_refresh_en, 0x1);
	SET_INNOPHY_REG(reg_calcs_sel, 0x2);
	SET_INNOPHY_REG(reg_start_calib, 0x1);

	BNE_INNOPHY_REG(calib_end, 0x1);
	BNE_INNOPHY_REG(calib_done_byte, 0x3);

	SET_INNOPHY_REG(reg_phy_refresh_en, 0x0);
	SET_INNOPHY_REG(reg_start_calib, 0x0);
	SET_INNOPHY_REG(reg_calcs_sel, 0x0);
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

static void ddrp_training(void)
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

	ddrp_dqs_calibration();

	/* DDR2 soft training */
	printf("DDR soft training ...\n");
	ddrp_training_read_calib_bypass();
	ddrp_training_soft_read_train(0x40);
	ddrp_training_write_train_bypass_dq(0x80);
	ddrp_training_soft_write_train();
}

static void ddrc_init(u32 kgd_rtt_dic)
{
	u32 data;

	ddrc_writel(MSTR, DDRC_MSTR);

	ddrc_writel(INIT0, DDRC_INIT0);
	ddrc_writel(INIT1, DDRC_INIT1);
	/* DDR2: INIT2 is LPDDR2-only; INIT5 is skipped. */
	ddrc_writel(INIT3, (DDRC_INIT3 & ~0x46) | kgd_rtt_dic);
	ddrc_writel(INIT4, DDRC_INIT4);

	ddrc_writel(ADDRMAP1, DDRC_ADDRMAP1);
	ddrc_writel(ADDRMAP2, DDRC_ADDRMAP2);
	ddrc_writel(ADDRMAP3, DDRC_ADDRMAP3);
	ddrc_writel(ADDRMAP4, DDRC_ADDRMAP4);
	ddrc_writel(ADDRMAP5, DDRC_ADDRMAP5);
	ddrc_writel(ADDRMAP6, DDRC_ADDRMAP6);

	ddrc_writel(DRAMTMG0, DDRC_TIMING0);
	ddrc_writel(DRAMTMG1, DDRC_TIMING1);
	ddrc_writel(DRAMTMG2, DDRC_TIMING2);
	ddrc_writel(DRAMTMG3, DDRC_TIMING3);
	ddrc_writel(DRAMTMG4, DDRC_TIMING4);
	ddrc_writel(DRAMTMG5, DDRC_TIMING5);
	ddrc_writel(DRAMTMG7, DDRC_TIMING7);
	ddrc_writel(DRAMTMG8, DDRC_TIMING8);	/* DDR2/DDR3 */

	ddrc_writel(DFITMG0, DDRC_DFITMG0);
	ddrc_writel(DFITMG1, DDRC_DFITMG1);
	ddrc_writel(DFIUPD0, DDRC_DFIUPD0);

	ddrc_writel(RFSHTMG, DDRC_RFSHTMG);
	ddrc_writel(RFSHCTL3, DDRC_RFSHCTL3);

	ddrc_writel(ODTCFG, DDRC_ODTCFG);	/* DDR2/LPDDR2 */

	data = ddrc_readl(SCHED);
	data &= ~(1 << 2);
	ddrc_writel(SCHED, data);
}

/*
 * Program the Innophy command/DQ pin-wrap selectors. Single fixed
 * chip (M14D5121632A) so no ddr_id switch.
 */
static void ddrp_map_set(void)
{
	static const unsigned char map[] = T32_DDR_PIN_MAP;

	SET_INNOPHY_REG(reg_cmd0_wrap_sel, map[0]);
	SET_INNOPHY_REG(reg_cmd1_wrap_sel, map[1]);
	SET_INNOPHY_REG(reg_cmd2_wrap_sel, map[2]);
	SET_INNOPHY_REG(reg_cmd3_wrap_sel, map[3]);
	SET_INNOPHY_REG(reg_cmd4_wrap_sel, map[4]);
	SET_INNOPHY_REG(reg_cmd5_wrap_sel, map[5]);
	SET_INNOPHY_REG(reg_cmd6_wrap_sel, map[6]);
	SET_INNOPHY_REG(reg_cmd7_wrap_sel, map[7]);
	SET_INNOPHY_REG(reg_cmd8_wrap_sel, map[8]);
	SET_INNOPHY_REG(reg_cmd9_wrap_sel, map[9]);
	SET_INNOPHY_REG(reg_cmd10_wrap_sel, map[10]);
	SET_INNOPHY_REG(reg_cmd11_wrap_sel, map[11]);
	SET_INNOPHY_REG(reg_cmd12_wrap_sel, map[12]);
	SET_INNOPHY_REG(reg_cmd13_wrap_sel, map[13]);
	SET_INNOPHY_REG(reg_cmd16_wrap_sel, map[14]);	/* WEB */
	SET_INNOPHY_REG(reg_cmd17_wrap_sel, map[15]);	/* CASB */
	SET_INNOPHY_REG(reg_cmd18_wrap_sel, map[16]);	/* RASB */
	SET_INNOPHY_REG(reg_cmd19_wrap_sel, map[17]);	/* BA0 */
	SET_INNOPHY_REG(reg_cmd20_wrap_sel, map[18]);	/* BA1 */
	SET_INNOPHY_REG(reg_cmd21_wrap_sel, map[19]);	/* BA2 */
	SET_INNOPHY_REG(reg_a_l_dm_bit_wrap_sel, map[20]);
	SET_INNOPHY_REG(reg_a_l_dq0_bit_wrap_sel, map[21]);
	SET_INNOPHY_REG(reg_a_l_dq1_bit_wrap_sel, map[22]);
	SET_INNOPHY_REG(reg_a_l_dq2_bit_wrap_sel, map[23]);
	SET_INNOPHY_REG(reg_a_l_dq3_bit_wrap_sel, map[24]);
	SET_INNOPHY_REG(reg_a_l_dq4_bit_wrap_sel, map[25]);
	SET_INNOPHY_REG(reg_a_l_dq5_bit_wrap_sel, map[26]);
	SET_INNOPHY_REG(reg_a_l_dq6_bit_wrap_sel, map[27]);
	SET_INNOPHY_REG(reg_a_l_dq7_bit_wrap_sel, map[28]);
	SET_INNOPHY_REG(reg_a_h_dm_bit_wrap_sel, map[29]);
	SET_INNOPHY_REG(reg_a_h_dq0_bit_wrap_sel, map[30]);
	SET_INNOPHY_REG(reg_a_h_dq1_bit_wrap_sel, map[31]);
	SET_INNOPHY_REG(reg_a_h_dq2_bit_wrap_sel, map[32]);
	SET_INNOPHY_REG(reg_a_h_dq3_bit_wrap_sel, map[33]);
	SET_INNOPHY_REG(reg_a_h_dq4_bit_wrap_sel, map[34]);
	SET_INNOPHY_REG(reg_a_h_dq5_bit_wrap_sel, map[35]);
	SET_INNOPHY_REG(reg_a_h_dq6_bit_wrap_sel, map[36]);
	SET_INNOPHY_REG(reg_a_h_dq7_bit_wrap_sel, map[37]);
}

static void ddrp_init(u32 ddrc_reset)
{
	u32 data;

	SET_INNOPHY_REG(reg_train_reg_update_en, 0x0);
	SET_INNOPHY_REG(reg_channel_en, DDRP_REG_CHANNEL_EN);
	SET_INNOPHY_REG(CWL_FRE_OP0, DDRP_CWL_FRE_OP0);
	SET_INNOPHY_REG(CL_FRE_OP0, DDRP_CL_FRE_OP0);
	SET_INNOPHY_REG(AL_FRE_OP0, DDRP_AL_FRE_OP0);
	SET_INNOPHY_REG(reg_phy_trfc, DDRP_REG_PHY_TRFC);
	SET_INNOPHY_REG(reg_phy_trefi, DDRP_REG_PHY_TREFI);
	SET_INNOPHY_REG(mem_select_t, DDRP_MEM_SELECT_T);

	SET_INNOPHY_REG(reg_pllpostdiven_fsp0, DDRP_REG_PLLPOSTDIVEN);
	SET_INNOPHY_REG(reg_pllpostdiven_fsp1, DDRP_REG_PLLPOSTDIVEN);
	SET_INNOPHY_REG(reg_pllpostdiven_fsp2, DDRP_REG_PLLPOSTDIVEN);
	SET_INNOPHY_REG(reg_pllpostdiven_fsp3, DDRP_REG_PLLPOSTDIVEN);
	SET_INNOPHY_REG(reg_pllpostdiv_fsp0, DDRP_REG_PLLPOSTDIV);
	SET_INNOPHY_REG(reg_pllpostdiv_fsp1, DDRP_REG_PLLPOSTDIV);
	SET_INNOPHY_REG(reg_pllpostdiv_fsp2, DDRP_REG_PLLPOSTDIV);
	SET_INNOPHY_REG(reg_pllpostdiv_fsp3, DDRP_REG_PLLPOSTDIV);

	SET_INNOPHY_REG(reg_pllcpp_bias_dqcmd, 0x4);
	SET_INNOPHY_REG(reg_pllcpi_bias_fsp0, 0x5);
	SET_INNOPHY_REG(reg_pllcpi_bias_fsp1, 0x5);
	SET_INNOPHY_REG(reg_pllcpi_bias_fsp2, 0x5);
	SET_INNOPHY_REG(reg_pllcpi_bias_fsp3, 0x5);

	ddrp_map_set();

	ddrc_reset &= ~0x10;	/* ddrc reset ok */
	writel(ddrc_reset, (void __iomem *)(CPM_BASE + CPM_SRBC0));
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

static void ddrc_dfi_init(void)
{
	u32 ddrc_reset;
	const u32 *p = t32_ddr_par;

	/* isolate ISP/VPU/IVDC/TNPU from the DDR domain during init */
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x20, 19, 19, 0);	/* ISP */
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x20, 28, 28, 0);	/* VPU */
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x28, 3, 3, 0);		/* IVDC */
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x28, 11, 11, 0);	/* TNPU */

	/* ddrc & ddrp soft reset (bit3/4 apb reset; bit4 ddrc reset) */
	ddrc_reset = readl((void __iomem *)(CPM_BASE + CPM_SRBC0));
	ddrc_reset |= 0x18;
	writel(ddrc_reset, (void __iomem *)(CPM_BASE + CPM_SRBC0));
	udelay(500);
	ddrc_reset &= ~0x8;	/* apb reset ok */
	writel(ddrc_reset, (void __iomem *)(CPM_BASE + CPM_SRBC0));
	udelay(500);

	ddrc_writel(SWCTLSTATIC, 0x1);
	ddrc_writel(SWCTL, 0x0);

	ddrc_init(p[T32_KGD_RTT_DIC]);
	ddrp_init(ddrc_reset);
	ddrp_set_drv_odt(p);
	ddrp_training();

	/* re-attach ISP/VPU/IVDC/TNPU */
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x20, 19, 19, 1);
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x20, 28, 28, 1);
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x28, 3, 3, 1);
	SET_INNOPHY_REG2(DDR_IO_BASE + 0x28, 11, 11, 1);

	ddrc_set_port_priority();
	ddrc_writel(SWCTLSTATIC, 0x0);
	ddrc_writel(SWCTL, 0x1);
}

void sdram_init(void)
{
	enable_cpu_read_ddr();

	printf("sdram init start\n");
	ddr_clk_init();
	ddrc_dfi_init();
	printf("sdram init Finished\n");
}

phys_size_t t32_sdram_size(void)
{
	return T32_DDR_SIZE;
}
