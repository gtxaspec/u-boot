/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T32 DDR: Synopsys uMCTL2 controller + Innophy training PHY
 * register map and per-SKU variant table.
 *
 * T32 (vendor PRJ007) is XBurst1 but does NOT use the legacy XBurst1
 * DDRC driven by ddr_t31.c: it pairs a Synopsys uMCTL2-class
 * controller (MSTR/INITn/DRAMTMGn/ADDRMAPn/DFITMGn/RFSHTMG ...) with
 * an Innophy training PHY, so this is its own driver/struct. The
 * per-SKU values (uMCTL2 GOLD words, PHY timing words, PLL/CPCCR
 * setpoints) live in struct ingenic_t32_ddr_params and are selected
 * at runtime from the devicetree via the node's compatible + the
 * driver of_match .data - no compile-time CONFIG_T32_VARIANT_*.
 *
 * Register map ported verbatim from the former
 * arch/mips/mach-xburst/include/mach/t32-ddr.h (itself the vendor
 * 2022.10 ddrc.h, last-definition-wins resolved). The Innophy pin-wrap
 * machinery (ddrp_map_set + the per-chip *_MAP tables) is deliberately
 * NOT here: vendor PRJ/ddr_innophy.c skips it for PRJ007/T32 silicon
 * (programming the swap table mis-wires CMD/DQ and the DQS-gating
 * calibration hangs); only PRJ008/T33 uses it.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#ifndef _DRIVERS_RAM_INGENIC_DDR_T32_H
#define _DRIVERS_RAM_INGENIC_DDR_T32_H

#include <stdio.h>		/* printf() in the PHY field-overflow check */
#include <asm/io.h>		/* readl()/writel() for the PHY inlines */
#include <linux/types.h>

/*
 * Base addresses (KSEG1 uncached). All off the SoC IO window
 * 0xb0000000. The uMCTL2 controller and the Innophy PHY each have
 * their own APB aperture; QOS is the AXI port-priority block. These
 * are distinct from the legacy DDRC_BASE (0xb34f0000) in <mach/t32.h>,
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
#define reg_a_h_ophsel				(DDRPAPB_BASE + 0x384), 7 , 5
#define reg_a_h_rd_train_dqs_default		(DDRPAPB_BASE + 0x358), 30, 24
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
#define reg_a_l_ophsel				(DDRPAPB_BASE + 0x284), 7 , 5
#define reg_a_l_rd_train_dqs_default		(DDRPAPB_BASE + 0x258), 30, 24
#define reg_a_l_rxmen0_delay_bp			(DDRPAPB_BASE + 0x208), 26, 24
#define reg_a_l_rxmen0_ophsel_bp		(DDRPAPB_BASE + 0x208), 23, 21
#define reg_a_l_rxmen0_sdlltap_bp		(DDRPAPB_BASE + 0x208), 20, 16
#define reg_calcs_sel				(DDRPAPB_BASE + 0x004), 3 , 2
#define reg_calib_bypass			(DDRPAPB_BASE + 0x004), 1 , 1
#define reg_calib_freq_update			(DDRPAPB_BASE + 0x04c), 27, 27
#define reg_channel_en				(DDRPAPB_BASE + 0x000), 16, 8
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
		printf("t32 ddr: phy field @%x [%u:%u] overflow %x\n",
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
 * DDR device type - selects the soft-train (DDR2) vs hardware-train
 * (DDR3/LPDDR3) Innophy path and the type-specific ddrc_init register
 * branches.
 */
enum ingenic_t32_ddr_type {
	T32_DDR_TYPE_DDR2 = 0,
	T32_DDR_TYPE_DDR3,
	T32_DDR_TYPE_LPDDR3,
};

/*
 * Per-SKU DDR configuration, deserialized from the &ddr node's
 * "ingenic,sdram-params" u32 array (the rk3328 DMC model). The struct is
 * all-u32 and its field order IS the DT array order, so of_to_plat reads it
 * in one shot. Values are the verbatim vendor ddr_creator_chip_v3 +
 * pll_params_creator output (PRJ007 class), carried per SKU in the board
 * leaf .dts. 44 cells.
 */
struct ingenic_t32_ddr_params {
	u32 type;			/* [0]  enum ingenic_t32_ddr_type */
	u32 size;			/* [1]  DRAM bytes */
	u32 ddr_ck_hz;			/* [2]  DDR CK = data rate / 2 */

	/*
	 * SPL PLL/CPCCR setpoints: CPAPCR/CPMPCR M/N/OD words and the
	 * two-stage CPCCR programming words (dividers, then source
	 * selects). Consumed by t32/pll.c in SPL via
	 * ingenic_t32_ddr_pll_setpoints() before driver model is up.
	 * (VPLL is SoC-fixed on every SKU and stays in pll.c.)
	 */
	u32 cpapcr;
	u32 cpmpcr;
	u32 cpccr_div;
	u32 cpccr_sel;

	/* uMCTL2 controller GOLD words (ddr_creator_chip_v3). */
	u32 mstr;
	u32 init0, init1, init3, init4, init5;
	u32 timing0, timing1, timing2, timing3, timing4;
	u32 timing5, timing6, timing7, timing8, timing14;
	u32 addrmap1, addrmap2, addrmap3, addrmap4, addrmap5, addrmap6;
	u32 rfshctl3, rfshtmg, odtcfg;
	u32 dfitmg0, dfitmg1, dfiupd0;

	/* Innophy PHY timing/config words. */
	u32 phy_channel_en;
	u32 phy_cwl, phy_cl, phy_al;
	u32 phy_trfc, phy_trefi;
	u32 phy_mem_sel;
	u32 phy_pllpostdiven, phy_pllpostdiv;
};

struct ingenic_t32_ddr_priv {
	u32 ram_size;			/* total bytes, for ram_get_info() */
};

/* Top-level DDR bring-up (ddr_t32.c), run once from the SPL probe. */
int ingenic_t32_ddr_sdram_init(const struct ingenic_t32_ddr_params *cfg);

/*
 * SPL helper for t32/pll.c: find the T32 DDR node in the FDT (by the single
 * ingenic,t32-ddr-innophy compatible) and return that SKU's PLL/CPCCR
 * setpoints from its ingenic,sdram-params array. Runs before driver model,
 * so the caller must have set
 * gd->fdt_blob (via fdtdec_setup()). Returns 0 on success, negative on
 * error.
 */
int ingenic_t32_ddr_pll_setpoints(u32 *cpapcr, u32 *cpmpcr,
				  u32 *cpccr_div, u32 *cpccr_sel);

#endif /* _DRIVERS_RAM_INGENIC_DDR_T32_H */
