/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T33 DDR: Synopsys uMCTL2 controller + Innophy training PHY
 *
 * Profile: T33 (vendor PRJ008) DDR2 M14D5121632A, 64 MB, 16-bit bus,
 *   CS0 only. DDR controller 650 MHz (L/DL bins; tCK 1538 ps) or
 *   550 MHz (VL/ZL bins, CONFIG_T33_DDR2_550). Same chip / pin MAP.
 *
 * This is NOT the legacy XBurst1 DDRC used by T10-T31. T33 has a
 * Synopsys uMCTL2-class controller (DDRC_MSTR/INITn/DRAMTMGn/
 * ADDRMAPn/DFITMGn/RFSHTMG ...) paired with an Innophy PHY driven
 * by a "soft training" sequence. The register values are the
 * known-good vendor host-tool output (ddr_creator_chip_v3, target
 * PRJ008_l = M14D5121632A); the Innophy field map is taken verbatim
 * (last-wins resolved) from the vendor 2022.10 ddrc.h so the SPL
 * init in t33/sdram.c can be diff-audited against the vendor
 * ddr_innophy.c DDR2 path. The init order, poll loops and delays
 * are timing-critical and reproduced exactly.
 *
 * Intentionally NOT ported (single fixed chip per defconfig;
 * upstream-friendly, no host tool / no generated header / no
 * global_reg_value indirection): LPDDR2/3 + DDR3 paths, hardware
 * training, the eFUSE-KGD hamming machinery (deferred vendor
 * factory mechanism) and the multi-chip match table.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __T33_DDR_H__
#define __T33_DDR_H__

#include <linux/types.h>

/*
 * Base addresses (KSEG1 uncached). All off the SoC IO window
 * 0xb0000000. The uMCTL2 controller and the Innophy PHY each have
 * their own APB aperture; QOS is the AXI port-priority block. These
 * are distinct from the legacy DDRC_BASE (0xb34f0000) in <mach/t33.h>,
 * which the uMCTL2 path does not use.
 */
#define DDR_IO_BASE		0xb0000000	/* ISP/VPU isolation pokes */
#define DDR_QOS_BASE		0xb3000000	/* AXI AR/AW QoS */
#define DDRCAPB_BASE		0xb3012000	/* uMCTL2 controller APB */
#define DDRPAPB_BASE		0xb3011000	/* Innophy PHY APB */

/* --- uMCTL2 controller registers (absolute addresses) --- */
#define MSTR			(DDRCAPB_BASE + 0x000)
#define STAT			(DDRCAPB_BASE + 0x004)
#define RFSHCTL3		(DDRCAPB_BASE + 0x060)
#define RFSHTMG			(DDRCAPB_BASE + 0x064)
#define INIT0			(DDRCAPB_BASE + 0x0d0)
#define INIT1			(DDRCAPB_BASE + 0x0d4)
#define INIT2			(DDRCAPB_BASE + 0x0d8)
#define INIT3			(DDRCAPB_BASE + 0x0dc)
#define INIT4			(DDRCAPB_BASE + 0x0e0)
#define INIT5			(DDRCAPB_BASE + 0x0e4)
#define DRAMTMG0		(DDRCAPB_BASE + 0x100)
#define DRAMTMG1		(DDRCAPB_BASE + 0x104)
#define DRAMTMG2		(DDRCAPB_BASE + 0x108)
#define DRAMTMG3		(DDRCAPB_BASE + 0x10c)
#define DRAMTMG4		(DDRCAPB_BASE + 0x110)
#define DRAMTMG5		(DDRCAPB_BASE + 0x114)
#define DRAMTMG6		(DDRCAPB_BASE + 0x118)
#define DRAMTMG7		(DDRCAPB_BASE + 0x11c)
#define DRAMTMG8		(DDRCAPB_BASE + 0x120)
#define DRAMTMG14		(DDRCAPB_BASE + 0x138)
#define DFITMG0			(DDRCAPB_BASE + 0x190)
#define DFITMG1			(DDRCAPB_BASE + 0x194)
#define DFIUPD0			(DDRCAPB_BASE + 0x1a0)
#define DFIMISC			(DDRCAPB_BASE + 0x1b0)
#define DFISTAT			(DDRCAPB_BASE + 0x1bc)
#define ADDRMAP1		(DDRCAPB_BASE + 0x204)
#define ADDRMAP2		(DDRCAPB_BASE + 0x208)
#define ADDRMAP3		(DDRCAPB_BASE + 0x20c)
#define ADDRMAP4		(DDRCAPB_BASE + 0x210)
#define ADDRMAP5		(DDRCAPB_BASE + 0x214)
#define ADDRMAP6		(DDRCAPB_BASE + 0x218)
#define ODTCFG			(DDRCAPB_BASE + 0x240)
#define SCHED			(DDRCAPB_BASE + 0x250)
#define SWCTL			(DDRCAPB_BASE + 0x320)
#define SWCTLSTATIC		(DDRCAPB_BASE + 0x328)
#define PCTRL0			(DDRCAPB_BASE + 0x490)
#define PCTRL1			(DDRCAPB_BASE + 0x540)
#define PCTRL2			(DDRCAPB_BASE + 0x5f0)
#define PCTRL3			(DDRCAPB_BASE + 0x6a0)
#define PCTRL4			(DDRCAPB_BASE + 0x750)
#define PCTRL5			(DDRCAPB_BASE + 0x800)
#define PCTRL6			(DDRCAPB_BASE + 0x8b0)
/* per-port config blocks: PCFGR0/PCFGW0/PCFGQOS00 + 0xb0 * port */
#define PCFGR0			(DDRCAPB_BASE + 0x404)
#define PCFGW0			(DDRCAPB_BASE + 0x408)
#define PCFGQOS00		(DDRCAPB_BASE + 0x494)

/*
 * --- Innophy PHY field map ---
 * Each field expands to "(addr), msb, lsb"; the SET/READ/BNE macros
 * below consume it as a bitfield descriptor. Taken from the vendor
 * 2022.10 ddrc.h with last-definition-wins resolved (the vendor
 * header redefines the *_invdelayselrx fields 2-3x: the a_l receive
 * fields end up width-8 [..:24=31:24], the a_h ones width-7
 * [30:24] - this asymmetry is deliberate and preserved).
 */
#define AL_FRE_OP0				(DDRPAPB_BASE + 0x008), 29, 24
#define calib_done_byte				(DDRPAPB_BASE + 0x174), 8 , 0
#define calib_end				(DDRPAPB_BASE + 0x174), 10, 10
#define CL_FRE_OP0				(DDRPAPB_BASE + 0x00c), 29, 24
#define CWL_FRE_OP0				(DDRPAPB_BASE + 0x010), 29, 24
#define mem_select_t				(DDRPAPB_BASE + 0x000), 6 , 4
#define reg_a_h_abutnrcompdq_reg		(DDRPAPB_BASE + 0x304), 28, 24
#define reg_a_h_abutodtpddq_reg			(DDRPAPB_BASE + 0x304), 12, 8
#define reg_a_h_abutodtpudq_reg			(DDRPAPB_BASE + 0x304), 4 , 0
#define reg_a_h_abutprcompdq_reg		(DDRPAPB_BASE + 0x304), 20, 16
#define reg_a_h_cs0_dm_invdelaysel		(DDRPAPB_BASE + 0x318), 26, 18
#define reg_a_h_cs0_dm_invdelayselrx		(DDRPAPB_BASE + 0x328), 14, 8
#define reg_a_h_cs0_dq0_invdelaysel		(DDRPAPB_BASE + 0x318), 17, 9
#define reg_a_h_cs0_dq0_invdelayselrx		(DDRPAPB_BASE + 0x32c), 30, 24
#define reg_a_h_cs0_dq1_invdelaysel		(DDRPAPB_BASE + 0x318), 8 , 0
#define reg_a_h_cs0_dq1_invdelayselrx		(DDRPAPB_BASE + 0x32c), 22, 16
#define reg_a_h_cs0_dq2_invdelaysel		(DDRPAPB_BASE + 0x31c), 26, 18
#define reg_a_h_cs0_dq2_invdelayselrx		(DDRPAPB_BASE + 0x32c), 14, 8
#define reg_a_h_cs0_dq3_invdelaysel		(DDRPAPB_BASE + 0x31c), 17, 9
#define reg_a_h_cs0_dq3_invdelayselrx		(DDRPAPB_BASE + 0x32c), 6 , 0
#define reg_a_h_cs0_dq4_invdelaysel		(DDRPAPB_BASE + 0x31c), 8 , 0
#define reg_a_h_cs0_dq4_invdelayselrx		(DDRPAPB_BASE + 0x330), 30, 24
#define reg_a_h_cs0_dq5_invdelaysel		(DDRPAPB_BASE + 0x320), 26, 18
#define reg_a_h_cs0_dq5_invdelayselrx		(DDRPAPB_BASE + 0x330), 22, 16
#define reg_a_h_cs0_dq6_invdelaysel		(DDRPAPB_BASE + 0x320), 17, 9
#define reg_a_h_cs0_dq6_invdelayselrx		(DDRPAPB_BASE + 0x330), 14, 8
#define reg_a_h_cs0_dq7_invdelaysel		(DDRPAPB_BASE + 0x320), 8 , 0
#define reg_a_h_cs0_dq7_invdelayselrx		(DDRPAPB_BASE + 0x330), 6 , 0
#define reg_a_h_cs0_dqsb_invdelaysel		(DDRPAPB_BASE + 0x324), 15, 8
#define reg_a_h_cs0_dqsb_invdelayselrx		(DDRPAPB_BASE + 0x334), 14, 8
#define reg_a_h_cs0_dqs_invdelaysel		(DDRPAPB_BASE + 0x324), 31, 24
#define reg_a_h_cs0_dqs_invdelayselrx		(DDRPAPB_BASE + 0x334), 30, 24
#define reg_a_h_cycsel				(DDRPAPB_BASE + 0x384), 10, 8
#define reg_a_h_dllsel				(DDRPAPB_BASE + 0x384), 4 , 0
#define reg_a_h_dm_bit_wrap_sel			(DDRPAPB_BASE + 0x35c), 13, 10
#define reg_a_h_dq0_bit_wrap_sel		(DDRPAPB_BASE + 0x360), 3 , 0
#define reg_a_h_dq1_bit_wrap_sel		(DDRPAPB_BASE + 0x360), 7 , 4
#define reg_a_h_dq2_bit_wrap_sel		(DDRPAPB_BASE + 0x360), 11, 8
#define reg_a_h_dq3_bit_wrap_sel		(DDRPAPB_BASE + 0x360), 15, 12
#define reg_a_h_dq4_bit_wrap_sel		(DDRPAPB_BASE + 0x360), 19, 16
#define reg_a_h_dq5_bit_wrap_sel		(DDRPAPB_BASE + 0x360), 23, 20
#define reg_a_h_dq6_bit_wrap_sel		(DDRPAPB_BASE + 0x360), 27, 24
#define reg_a_h_dq7_bit_wrap_sel		(DDRPAPB_BASE + 0x360), 31, 28
#define reg_a_h_ophsel				(DDRPAPB_BASE + 0x384), 7 , 5
#define reg_a_h_rd_train_dqs_default		(DDRPAPB_BASE + 0x358), 30, 24
#define reg_a_h_rxen_lp4			(DDRPAPB_BASE + 0x310), 23, 23
#define reg_a_h_rxmen0_delay_bp			(DDRPAPB_BASE + 0x308), 26, 24
#define reg_a_h_rxmen0_ophsel_bp		(DDRPAPB_BASE + 0x308), 23, 21
#define reg_a_h_rxmen0_sdlltap_bp		(DDRPAPB_BASE + 0x308), 20, 16
#define reg_a_l_abutnrcompdq_reg		(DDRPAPB_BASE + 0x204), 28, 24
#define reg_a_l_abutodtpddq_reg			(DDRPAPB_BASE + 0x204), 12, 8
#define reg_a_l_abutodtpudq_reg			(DDRPAPB_BASE + 0x204), 4 , 0
#define reg_a_l_abutprcompdq_reg		(DDRPAPB_BASE + 0x204), 20, 16
#define reg_a_l_cs0_dm_invdelaysel		(DDRPAPB_BASE + 0x218), 26, 18
#define reg_a_l_cs0_dm_invdelayselrx		(DDRPAPB_BASE + 0x228), 15, 8
#define reg_a_l_cs0_dq0_invdelaysel		(DDRPAPB_BASE + 0x218), 17, 9
#define reg_a_l_cs0_dq0_invdelayselrx		(DDRPAPB_BASE + 0x22c), 31, 24
#define reg_a_l_cs0_dq1_invdelaysel		(DDRPAPB_BASE + 0x218), 8 , 0
#define reg_a_l_cs0_dq1_invdelayselrx		(DDRPAPB_BASE + 0x22c), 23, 16
#define reg_a_l_cs0_dq2_invdelaysel		(DDRPAPB_BASE + 0x21c), 26, 18
#define reg_a_l_cs0_dq2_invdelayselrx		(DDRPAPB_BASE + 0x22c), 15, 8
#define reg_a_l_cs0_dq3_invdelaysel		(DDRPAPB_BASE + 0x21c), 17, 9
#define reg_a_l_cs0_dq3_invdelayselrx		(DDRPAPB_BASE + 0x22c), 7 , 0
#define reg_a_l_cs0_dq4_invdelaysel		(DDRPAPB_BASE + 0x21c), 8 , 0
#define reg_a_l_cs0_dq4_invdelayselrx		(DDRPAPB_BASE + 0x230), 31, 24
#define reg_a_l_cs0_dq5_invdelaysel		(DDRPAPB_BASE + 0x220), 26, 18
#define reg_a_l_cs0_dq5_invdelayselrx		(DDRPAPB_BASE + 0x230), 23, 16
#define reg_a_l_cs0_dq6_invdelaysel		(DDRPAPB_BASE + 0x220), 17, 9
#define reg_a_l_cs0_dq6_invdelayselrx		(DDRPAPB_BASE + 0x230), 15, 8
#define reg_a_l_cs0_dq7_invdelaysel		(DDRPAPB_BASE + 0x220), 8 , 0
#define reg_a_l_cs0_dq7_invdelayselrx		(DDRPAPB_BASE + 0x230), 7 , 0
#define reg_a_l_cs0_dqsb_invdelaysel		(DDRPAPB_BASE + 0x224), 15, 8
#define reg_a_l_cs0_dqsb_invdelayselrx		(DDRPAPB_BASE + 0x234), 15, 8
#define reg_a_l_cs0_dqs_invdelaysel		(DDRPAPB_BASE + 0x224), 31, 24
#define reg_a_l_cs0_dqs_invdelayselrx		(DDRPAPB_BASE + 0x234), 31, 24
#define reg_a_l_cycsel				(DDRPAPB_BASE + 0x284), 10, 8
#define reg_a_l_dllsel				(DDRPAPB_BASE + 0x284), 4 , 0
#define reg_a_l_dm_bit_wrap_sel			(DDRPAPB_BASE + 0x25c), 13, 10
#define reg_a_l_dq0_bit_wrap_sel		(DDRPAPB_BASE + 0x260), 3 , 0
#define reg_a_l_dq1_bit_wrap_sel		(DDRPAPB_BASE + 0x260), 7 , 4
#define reg_a_l_dq2_bit_wrap_sel		(DDRPAPB_BASE + 0x260), 11, 8
#define reg_a_l_dq3_bit_wrap_sel		(DDRPAPB_BASE + 0x260), 15, 12
#define reg_a_l_dq4_bit_wrap_sel		(DDRPAPB_BASE + 0x260), 19, 16
#define reg_a_l_dq5_bit_wrap_sel		(DDRPAPB_BASE + 0x260), 23, 20
#define reg_a_l_dq6_bit_wrap_sel		(DDRPAPB_BASE + 0x260), 27, 24
#define reg_a_l_dq7_bit_wrap_sel		(DDRPAPB_BASE + 0x260), 31, 28
#define reg_a_l_ophsel				(DDRPAPB_BASE + 0x284), 7 , 5
#define reg_a_l_rd_train_dqs_default		(DDRPAPB_BASE + 0x258), 30, 24
#define reg_a_l_rxen_lp4			(DDRPAPB_BASE + 0x210), 23, 23
#define reg_a_l_rxmen0_delay_bp			(DDRPAPB_BASE + 0x208), 26, 24
#define reg_a_l_rxmen0_ophsel_bp		(DDRPAPB_BASE + 0x208), 23, 21
#define reg_a_l_rxmen0_sdlltap_bp		(DDRPAPB_BASE + 0x208), 20, 16
#define reg_calcs_sel				(DDRPAPB_BASE + 0x004), 3 , 2
#define reg_calib_bypass			(DDRPAPB_BASE + 0x004), 1 , 1
#define reg_calib_freq_update			(DDRPAPB_BASE + 0x04c), 27, 27
#define reg_channel_en				(DDRPAPB_BASE + 0x000), 16, 8
#define reg_cmd0_wrap_sel			(DDRPAPB_BASE + 0x060), 29, 25
#define reg_cmd10_wrap_sel			(DDRPAPB_BASE + 0x064), 9 , 5
#define reg_cmd11_wrap_sel			(DDRPAPB_BASE + 0x064), 4 , 0
#define reg_cmd12_wrap_sel			(DDRPAPB_BASE + 0x068), 29, 25
#define reg_cmd13_wrap_sel			(DDRPAPB_BASE + 0x068), 24, 20
#define reg_cmd16_wrap_sel			(DDRPAPB_BASE + 0x068), 9 , 5
#define reg_cmd17_wrap_sel			(DDRPAPB_BASE + 0x068), 4 , 0
#define reg_cmd18_wrap_sel			(DDRPAPB_BASE + 0x06c), 29, 25
#define reg_cmd19_wrap_sel			(DDRPAPB_BASE + 0x06c), 24, 20
#define reg_cmd1_wrap_sel			(DDRPAPB_BASE + 0x060), 24, 20
#define reg_cmd20_wrap_sel			(DDRPAPB_BASE + 0x06c), 19, 15
#define reg_cmd21_wrap_sel			(DDRPAPB_BASE + 0x06c), 14, 10
#define reg_cmd2_wrap_sel			(DDRPAPB_BASE + 0x060), 19, 15
#define reg_cmd3_wrap_sel			(DDRPAPB_BASE + 0x060), 14, 10
#define reg_cmd4_wrap_sel			(DDRPAPB_BASE + 0x060), 9 , 5
#define reg_cmd5_wrap_sel			(DDRPAPB_BASE + 0x060), 4 , 0
#define reg_cmd6_wrap_sel			(DDRPAPB_BASE + 0x064), 29, 25
#define reg_cmd7_wrap_sel			(DDRPAPB_BASE + 0x064), 24, 20
#define reg_cmd8_wrap_sel			(DDRPAPB_BASE + 0x064), 19, 15
#define reg_cmd9_wrap_sel			(DDRPAPB_BASE + 0x064), 14, 10
#define reg_cmd_abutnrcomp_ck0_reg		(DDRPAPB_BASE + 0x0c8), 12, 8
#define reg_cmd_abutnrcomp_reg			(DDRPAPB_BASE + 0x0c8), 28, 24
#define reg_cmd_abutprcomp_ck0_reg		(DDRPAPB_BASE + 0x0c8), 4 , 0
#define reg_cmd_abutprcomp_reg			(DDRPAPB_BASE + 0x0c8), 20, 16
#define reg_dq_rd_train_en			(DDRPAPB_BASE + 0x0a4), 0 , 0
#define reg_dq_wr_train_auto			(DDRPAPB_BASE + 0x0b0), 0 , 0
#define reg_dq_wr_train_en			(DDRPAPB_BASE + 0x0b0), 1 , 1
#define reg_phy_refresh_en			(DDRPAPB_BASE + 0x0b8), 0 , 0
#define reg_phy_trefi				(DDRPAPB_BASE + 0x0b8), 31, 18
#define reg_phy_trfc				(DDRPAPB_BASE + 0x0b8), 17, 8
#define reg_pllcpi_bias_fsp0			(DDRPAPB_BASE + 0x01c), 2 , 0
#define reg_pllcpi_bias_fsp1			(DDRPAPB_BASE + 0x01c), 10, 8
#define reg_pllcpi_bias_fsp2			(DDRPAPB_BASE + 0x01c), 18, 16
#define reg_pllcpi_bias_fsp3			(DDRPAPB_BASE + 0x01c), 26, 24
#define reg_pllcpp_bias_dqcmd			(DDRPAPB_BASE + 0x088), 10, 8
#define reg_pllpostdiven_fsp0			(DDRPAPB_BASE + 0x01c), 3 , 3
#define reg_pllpostdiven_fsp1			(DDRPAPB_BASE + 0x01c), 11, 11
#define reg_pllpostdiven_fsp2			(DDRPAPB_BASE + 0x01c), 19, 19
#define reg_pllpostdiven_fsp3			(DDRPAPB_BASE + 0x01c), 27, 27
#define reg_pllpostdiv_fsp0			(DDRPAPB_BASE + 0x01c), 6 , 4
#define reg_pllpostdiv_fsp1			(DDRPAPB_BASE + 0x01c), 14, 12
#define reg_pllpostdiv_fsp2			(DDRPAPB_BASE + 0x01c), 22, 20
#define reg_pllpostdiv_fsp3			(DDRPAPB_BASE + 0x01c), 30, 28
#define reg_rdtrain_cs_sel			(DDRPAPB_BASE + 0x0a4), 9 , 8
#define reg_rd_train_dqs_range_bypass		(DDRPAPB_BASE + 0x0a4), 6 , 6
#define reg_rd_train_freq_update		(DDRPAPB_BASE + 0x0a4), 2 , 2
#define reg_start_calib				(DDRPAPB_BASE + 0x004), 0 , 0
#define reg_train_reg_update_en			(DDRPAPB_BASE + 0x08c), 18, 18
#define reg_wl_bypass				(DDRPAPB_BASE + 0x004), 5 , 5
#define reg_wlcs_sel				(DDRPAPB_BASE + 0x004), 7 , 6
#define reg_wl_enable				(DDRPAPB_BASE + 0x004), 4 , 4
#define reg_wl_end				(DDRPAPB_BASE + 0x174), 11, 11
#define reg_wl_freq_update			(DDRPAPB_BASE + 0x04c), 28, 28
#define reg_wl_loadmode				(DDRPAPB_BASE + 0x004), 31, 16
#define reg_wrtrain_check_data_value_random_gen	(DDRPAPB_BASE + 0x0b0), 8 , 8
#define reg_wrtrain_cs_sel			(DDRPAPB_BASE + 0x0b0), 7 , 6
#define reg_wr_train_dqs_default_bypass		(DDRPAPB_BASE + 0x0b0), 4 , 4
#define train_all_step_done			(DDRPAPB_BASE + 0x158), 7 , 7
#define train_step1_delay_done			(DDRPAPB_BASE + 0x158), 6 , 6
#define train_true_done				(DDRPAPB_BASE + 0x158), 0 , 0
#define wl_done_byte				(DDRPAPB_BASE + 0x174), 24, 16

/* --- uMCTL2 register accessors (full address) --- */
#define ddrc_writel(addr, value)	writel((value), (void __iomem *)(addr))
#define ddrc_readl(addr)		readl((void __iomem *)(addr))

/*
 * --- Innophy PHY bitfield accessors ---
 * The variadic-via-comma form lets a field macro (which expands to
 * "addr, msb, lsb") be passed as a single token: SET_INNOPHY_REG(f, v)
 * becomes SET_INNOPHY_REG2(addr, msb, lsb, v). The raw _REG2 form is
 * also called directly (e.g. for the ISP/VPU isolation pokes).
 */
static inline u32 READ_INNOPHY_REG2(u32 addr, u32 msb, u32 lsb)
{
	u32 v = readl((void __iomem *)addr) >> lsb;

	v &= (1u << (msb - lsb + 1)) - 1;
	return v;
}

static inline void SET_INNOPHY_REG2(u32 addr, u32 msb, u32 lsb, u32 data)
{
	u32 mask = (1u << (msb - lsb + 1)) - 1;
	u32 v;

	if (data > mask) {
		printf("t33 ddr: phy field @%x [%u:%u] overflow %x\n",
		       addr, msb, lsb, data);
		return;
	}
	v = readl((void __iomem *)addr);
	v &= ~(mask << lsb);
	v |= data << lsb;
	writel(v, (void __iomem *)addr);
}

static inline u32 BNE_INNOPHY_REG2(u32 addr, u32 msb, u32 lsb, u32 data)
{
	u32 mask = 0xffffffffu >> (31 - msb);
	u32 v;

	do {
		v = (readl((void __iomem *)addr) & mask) >> lsb;
	} while (v != data);
	return v;
}

#define READ_INNOPHY_REG(f)		READ_INNOPHY_REG2(f)
#define SET_INNOPHY_REG(f, v)		SET_INNOPHY_REG2(f, v)
#define BNE_INNOPHY_REG(f, v)		BNE_INNOPHY_REG2(f, v)

/*
 * --- Innophy command/DQ pin-wrap map indices ---
 * The per-chip *_MAP array holds one of these per PHY lane; ddrp_map_set
 * programs the reg_cmdN_wrap_sel / reg_a_[lh]_dqN_bit_wrap_sel fields
 * from it. (Vendor asm/arch/ddr_innophy.h enums.)
 */
enum {
	DDRP_A0, DDRP_A1, DDRP_A2, DDRP_A3, DDRP_A4, DDRP_A5, DDRP_A6,
	DDRP_A7, DDRP_A8, DDRP_A9, DDRP_A10, DDRP_A11, DDRP_A12, DDRP_A13,
	DDRP_A14, DDRP_A15, DDRP_WEB, DDRP_CASB, DDRP_RASB,
	DDRP_BA0, DDRP_BA1, DDRP_BA2,
};
enum {
	DDRP_DQ0, DDRP_DQ1, DDRP_DQ2, DDRP_DQ3, DDRP_DQ4, DDRP_DQ5,
	DDRP_DQ6, DDRP_DQ7, DDRP_DM0,
};
enum {
	DDRP_DQ8, DDRP_DQ9, DDRP_DQ10, DDRP_DQ11, DDRP_DQ12, DDRP_DQ13,
	DDRP_DQ14, DDRP_DQ15, DDRP_DM1,
};

/* M14D5121632A PHY pin-wrap map (38 lanes), vendor ddr_creator output */
#define T33_DDR_PIN_MAP { \
	DDRP_A10, DDRP_A3, DDRP_A6, DDRP_A1, DDRP_A5, DDRP_A4, DDRP_A11, \
	DDRP_A9, DDRP_A12, DDRP_A7, DDRP_BA0, DDRP_A8, DDRP_A2, DDRP_A13, \
	DDRP_BA1, DDRP_RASB, DDRP_WEB, DDRP_CASB, DDRP_A0, DDRP_BA2, \
	DDRP_DM0, DDRP_DQ6, DDRP_DQ3, DDRP_DQ1, DDRP_DQ4, DDRP_DQ2, \
	DDRP_DQ7, DDRP_DQ5, DDRP_DQ0, DDRP_DQ13, DDRP_DQ10, DDRP_DQ12, \
	DDRP_DQ8, DDRP_DQ15, DDRP_DQ14, DDRP_DQ11, DDRP_DQ9, DDRP_DM1 }

/*
 * --- DDR2 known-good controller/PHY parameter set ---
 * M14D5121632A, host ddr_creator_chip_v3 (target PRJ008_l). Used
 * directly by ddrc_init()/ddrp_init(); single fixed chip, so no
 * runtime match table.
 */
#define T33_DDR_TYPE_DDR2	0x1111
#define T33_DDR_SIZE		0x04000000U	/* 64 MB */

/*
 * Variant-varying DDR2 set. The vendor PRJ008 goat bins share the
 * same M14D5121632A chip / pin MAP / 64 MB; they differ only in the
 * DDR clock and the dependent uMCTL2 + Innophy timing. Values are
 * the exact host ddr_creator_chip_v3 output: PRJ008_l/dl (default,
 * 650 MHz) vs PRJ008_vl/zl (CONFIG_T33_DDR2_550, 550 MHz). Every
 * other DDRC_/DDRP_ value below is bin-invariant.
 */
#if defined(CONFIG_T33_DDR2_550)
#define T33_DDR_FREQ		550000000U	/* VL/ZL data rate */
#define DDRC_INIT0		0x00010036
#define DDRC_INIT3		0x0f130000
#define DDRC_TIMING0		0x0901120c
#define DDRC_TIMING1		0x00020310
#define DDRC_TIMING4		0x04010304
#define DDRC_TIMING8		0x00000401
#define DDRC_RFSHTMG		0x0043001d
#define DDRP_REG_PHY_TRFC	0x0000001d
#define DDRP_REG_PHY_TREFI	0x00000862
#else
#define T33_DDR_FREQ		650000000U	/* L/DL data rate */
#define DDRC_INIT0		0x00010040
#define DDRC_INIT3		0x01130000
#define DDRC_TIMING0		0x0a01150f
#define DDRC_TIMING1		0x00020313
#define DDRC_TIMING4		0x04010405
#define DDRC_TIMING8		0x00000402
#define DDRC_RFSHTMG		0x004f0023
#define DDRP_REG_PHY_TRFC	0x00000023
#define DDRP_REG_PHY_TREFI	0x000009e8
#endif

/*
 * Compile-time constants (vendor names: the controller register
 * VALUE is DDRC_<reg>, distinct from the unprefixed register ADDRESS
 * macro <reg> above, so t33/sdram.c reads near-verbatim against the
 * vendor ddr_innophy.c). MR0=0x0113/MR1=0x0000 (INIT3),
 * MR2=0x0000/MR3=0x0000 (INIT4). These are bin-invariant; the
 * clock-dependent regs (INIT0/INIT3/TIMING0/1/4/8, RFSHTMG, PHY
 * TRFC/TREFI) live in the CONFIG_T33_DDR2_550 block above.
 */
#define DDRC_MSTR		0x00040000
#define DDRC_INIT1		0x00000007
#define DDRC_INIT2		0x00000000
#define DDRC_INIT4		0x00000000
#define DDRC_INIT5		0x00000000
#define DDRC_TIMING2		0x00000408
#define DDRC_TIMING3		0x00001000
#define DDRC_TIMING5		0x01010202
#define DDRC_TIMING6		0x00000000
#define DDRC_TIMING7		0x00000101
#define DDRC_TIMING14		0x00000000
#define DDRC_ADDRMAP1		0x003f1515
#define DDRC_ADDRMAP2		0x00000000
#define DDRC_ADDRMAP3		0x00000000
#define DDRC_ADDRMAP4		0x00001f1f
#define DDRC_ADDRMAP5		0x04040404
#define DDRC_ADDRMAP6		0x0f0f0f04
#define DDRC_RFSHCTL3		0x00000002
#define DDRC_ODTCFG		0x07020708
#define DDRC_DFITMG0		0x06020002
#define DDRC_DFITMG1		0x00080307
#define DDRC_DFIUPD0		0x80400003
#define DDRP_REG_CHANNEL_EN	0x00000003
#define DDRP_CWL_FRE_OP0	0x00000007
#define DDRP_CL_FRE_OP0		0x00000008
#define DDRP_AL_FRE_OP0		0x00000000
#define DDRP_MEM_SELECT_T	0x00000000
#define DDRP_REG_PLLPOSTDIVEN	0x00000000
#define DDRP_REG_PLLPOSTDIV	0x00000000

/*
 * --- DDR2 drive-strength / ODT parameters ---
 * Indices into the par array (vendor enum order). With no programmed
 * eFUSE-KGD (the case for this port), ddr_par_init() supplies these
 * fixed DDR2 defaults; the KGD/SKEW/INDEX entries are unused here.
 */
enum {
	T33_ODT_PD, T33_ODT_PU, T33_CMD_RC_PD, T33_CMD_RC_PU,
	T33_CLK_RC_PD, T33_CLK_RC_PU, T33_DQX_RC_PD, T33_DQX_RC_PU,
	T33_VREF, T33_KGD_ODT, T33_KGD_DS, T33_KGD_RTT_DIC,
	T33_DDR_PAR_NUM,
};

/* DDR2 ddr_par_init() defaults (no eFUSE); KGD_RTT_DIC resolves to 0. */
#define T33_DDR2_DRV_ODT_DEFAULTS { \
	[T33_ODT_PD] = 0x00, [T33_ODT_PU] = 0x00, \
	[T33_CMD_RC_PD] = 0x08, [T33_CMD_RC_PU] = 0x08, \
	[T33_CLK_RC_PD] = 0x08, [T33_CLK_RC_PU] = 0x08, \
	[T33_DQX_RC_PD] = 0x08, [T33_DQX_RC_PU] = 0x08, \
	[T33_VREF] = 0x70, [T33_KGD_ODT] = 0x00, \
	[T33_KGD_DS] = 0x00, [T33_KGD_RTT_DIC] = 0x00 }

#endif /* __T33_DDR_H__ */
