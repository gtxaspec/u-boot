/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T21 DDR2 controller and Innophy PHY register map
 *
 * Profile: isvp_t21 (T21N) DDR2 M14D5121632A, 64 MB, 16-bit bus,
 * single CS0, 4-bank, DDR clock 450 MHz (MPLL 900 / 2).
 *
 * T21 uses the SAME Innophy DDR2 PHY and the SAME 64 MB part
 * (M14D5121632A, row 13 / col 10 / 4-bank) as T23, so the geometry
 * (DDRC_CFG / MMAP0 / MMAP1) is identical to the T23 64 MB set and
 * is clock-independent. The clock differs (450 vs T23's 600 MHz),
 * so DDRC_REFCNT / TIMING1-6 / MR0 are the 450 MHz set. MR0 also
 * differs (0x0c73 vs T23/T31 0x0f73): T21 is NOT in the vendor
 * DDR2_CHIP_MR0_DLL_RST list (isvp_common.h), so it takes the
 * #else MR0 path.
 *
 * All register values are the exact vendor ddr_params_creator
 * output for the isvp_t21_sfcnor (T21N) build. The host-compiled
 * creator was validated by reproducing the T31 128 MB GOLD
 * bit-for-bit before trusting the T21 output.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __T21_DDR_H__
#define __T21_DDR_H__

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
#define T21_DQS_DELAY_L		(DDR_PHY_BASE + 0x120)
#define T21_DQS_DELAY_H		(DDR_PHY_BASE + 0x160)
#define T21_REG46		(DDR_PHY_BASE + 0x118)
#define T21_REG56		(DDR_PHY_BASE + 0x158)
#define T21_INIT_COMP		(DDR_PHY_BASE + 0xd0)

#define DDR_APB_PHY_INIT	(DDR_APB_BASE + 0x8c)
#define REG_DDR_CTRL		(DDRC_BASE + 0x008)
#define REG_DDR_CFG		(DDRC_BASE + 0x004)
#define REG_DDR_LMR		(DDRC_BASE + 0x00c)

/* CPM registers used for the DDR clock divider and DLL reset */
#define CPM_DRCG		0xd0
#define CPM_DDRCDR		0x2c

/*
 * Chip geometry: M14D5121632A 64 MB, row 13 / col 10, 4-bank,
 * 16-bit, single CS0 (same part as T23 - no T21 board is 128 MB).
 */
#define DDR_ROW			13
#define DDR_COL			10
#define DDR_BANK8		0
#define CONFIG_DDR_DW32		0
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0

/*
 * Clock targets, per variant (DDR = MPLL/2, cdr=1): T21N
 * MPLL 900 / DDR 450; HIGH_PERF MPLL 1000 / DDR 500.
 */
#if defined(CONFIG_T21_VARIANT_HP)
#define DDR_MPLL_RATE		1000000000U
#define DDR_TARGET_RATE		500000000U
#else
#define DDR_MPLL_RATE		900000000U
#define DDR_TARGET_RATE		450000000U
#endif

/*
 * GOLD register values: exact vendor ddr_params_creator output
 * for isvp_t21_sfcnor (M14D5121632A 64 MB). CFG/MMAP/CTRL and
 * TIMING5 are geometry/clock-invariant; REFCNT/TIMING1-4/6/MR0
 * are the clock-dependent set - T21N @450, HIGH_PERF @500.
 */
#define DDRC_CFG_VALUE		0x0a288a40
#define DDRC_CTRL_VALUE		0x0000d91e
#define DDRC_MMAP0_VALUE	0x000020fc
#define DDRC_MMAP1_VALUE	0x00002400
#define DDRC_TIMING5_VALUE	0xff060405
#if defined(CONFIG_T21_VARIANT_HP)
#define DDRC_REFCNT_VALUE	0x00f20001
#define DDRC_TIMING1_VALUE	0x040e0806
#define DDRC_TIMING2_VALUE	0x02170707
#define DDRC_TIMING3_VALUE	0x2007051e
#define DDRC_TIMING4_VALUE	0x1a240031
#define DDRC_TIMING6_VALUE	0x32170505
#define DDRP_MR0_VALUE		0x00000e73
#else
#define DDRC_REFCNT_VALUE	0x00da0001
#define DDRC_TIMING1_VALUE	0x040e0706
#define DDRC_TIMING2_VALUE	0x02150607
#define DDRC_TIMING3_VALUE	0x2006051b
#define DDRC_TIMING4_VALUE	0x17240031
#define DDRC_TIMING6_VALUE	0x32150505
#define DDRP_MR0_VALUE		0x00000c73
#endif

#define DDR_CHIP_0_SIZE		67108864	/* 64 MB */
#define DDR_CHIP_1_SIZE		0

#endif /* __T21_DDR_H__ */
