/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T23 DDR2 controller and Innophy PHY register map
 *
 * Profile: isvp_t23 DDR2, M14D1G1664A, 128 MB, 16-bit bus, CS0 only,
 *   DDR clock 650 MHz (MPLL 1300 MHz / 2).
 *
 * T23 uses the SAME Innophy DDR2 PHY + DDR2 part (M14D1G1664A) as
 * T31; only the DDR clock differs (650 vs T31's 600 MHz; MPLL 1300
 * vs 1200, but the MPLL->DDR divider is /2 in both, so cdr = 1).
 *
 * The DDRC_TIMINGn / REFCNT / MR0 values are clock-cycle counts
 * the vendor host params creator computes from the part's ns
 * timings and the DDR clock. The GOLD block below is the T31
 * 600 MHz set reused as the empirical starting point for 650 MHz
 * (same part). If dram_verify() fails on the rig these are retuned
 * to the vendor 650 MHz computed values - see
 * project_uboot_tseries_port_scope.md.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __T23_DDR_H__
#define __T23_DDR_H__

/* Base addresses (KSEG1 uncached) */
#define DDRC_BASE		0xb34f0000
#define DDR_PHY_BASE		0xb3011000
#define DDR_APB_BASE		0xb3012000

/* DDR controller register offsets */
#define DDRC_STATUS		0x000
#define DDRC_CFG		0x004
#define DDRC_CTRL		0x008
#define DDRC_LMR		0x00c
#define DDRC_REFCNT		0x018
#define DDRC_MMAP0		0x024
#define DDRC_MMAP1		0x028
#define DDRC_DLP		0x0bc
#define DDRC_AUTOSR_EN		0x304
#define DDRC_TIMING(n)		(0x060 + 4 * ((n) - 1))
#define DDRC_REMAP(n)		(0x09c + 4 * ((n) - 1))

#define DDRC_DSTATUS_MISS	(1 << 6)
#define DDRC_CTRL_DFI_RST	(1 << 23)
#define DDRC_CTRL_ALH		(1 << 3)
#define DDRC_CTRL_CKE		(1 << 1)

/* Innophy PHY register offsets (added to DDR_PHY_BASE) */
#define INNO_CHANNEL_EN		0x00
#define INNO_MEM_CFG		0x04
#define INNO_TRAINING_CTRL	0x08
#define INNO_CL			0x14
#define INNO_AL			0x18
#define INNO_CWL		0x1c
#define INNO_DQ_WIDTH		0x7c
#define INNO_PLL_FBDIV		0x80
#define INNO_PLL_CTRL		0x84
#define INNO_PLL_PDIV		0x88
#define INNO_WL_DONE		0xc0
#define INNO_PLL_LOCK		0xc8
#define INNO_CALIB_DONE		0xcc
#define INNO_INIT_COMP		0xd0

/* Absolute PHY/APB/AHB addresses used by the init sequence */
#define T23_DQS_DELAY_L		(DDR_PHY_BASE + 0x120)
#define T23_DQS_DELAY_H		(DDR_PHY_BASE + 0x160)
#define T23_REG46		(DDR_PHY_BASE + 0x118)
#define T23_REG56		(DDR_PHY_BASE + 0x158)
#define T23_INIT_COMP		(DDR_PHY_BASE + 0xd0)

#define DDR_APB_PHY_INIT	(DDR_APB_BASE + 0x8c)
#define REG_DDR_CTRL		(DDRC_BASE + 0x008)
#define REG_DDR_CFG		(DDRC_BASE + 0x004)
#define REG_DDR_LMR		(DDRC_BASE + 0x00c)

/* CPM registers used for the DDR clock divider and DLL reset */
#define CPM_DRCG		0xd0
#define CPM_DDRCDR		0x2c

/* Chip geometry from include/ddr/chips/DDR2_M14D1G1664A.h (DW32=0) */
#define DDR_ROW			13
#define DDR_COL			10
#define DDR_BANK8		1
#define CONFIG_DDR_DW32		0
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0

/* Clock targets for this profile: T23 MPLL 1300 MHz, DDR 650 MHz */
#define DDR_MPLL_RATE		1300000000U
#define DDR_TARGET_RATE		650000000U

/*
 * GOLD register values. Reused from the T31 600 MHz known-good build
 * (same M14D1G1664A part / same Innophy DDR2 path) as the empirical
 * first shot at 650 MHz. Retune here to the vendor 650 MHz computed
 * set if dram_verify() reports a failure.
 */
#define DDRC_CFG_VALUE		0x0aa88a42
#define DDRC_CTRL_VALUE		0x0000d91e
#define DDRC_MMAP0_VALUE	0x000020f8
#define DDRC_MMAP1_VALUE	0x00002800
#define DDRC_REFCNT_VALUE	0x00910003
#define DDRC_TIMING1_VALUE	0x050f0a06
#define DDRC_TIMING2_VALUE	0x021c0a07
#define DDRC_TIMING3_VALUE	0x200a0722
#define DDRC_TIMING4_VALUE	0x26240031
#define DDRC_TIMING5_VALUE	0xff060405
#define DDRC_TIMING6_VALUE	0x321c0505
#define DDRP_MR0_VALUE		0x00000f73

#define DDR_CHIP_0_SIZE		134217728	/* 128 MB */
#define DDR_CHIP_1_SIZE		0

#endif /* __T23_DDR_H__ */
