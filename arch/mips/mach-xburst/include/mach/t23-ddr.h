/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T23 DDR2 controller and Innophy PHY register map
 *
 * Profile: isvp_t23 DDR2, 16-bit bus, single CS0, DDR 600 MHz
 *   (MPLL 1200 / 2). Standard T23/T23N = M14D5121632A 64 MB
 *   (row13/col10/4-bank); T23DL/T23DN = M14D2561616A 32 MB
 *   (row13/col9/4-bank). No T23 board is 128 MB.
 *
 * T23 uses the SAME Innophy DDR2 PHY as T31 and the SAME DDR
 * clock (600 MHz off MPLL 1200, cdr = 1). The only deltas vs the
 * T31 128 MB profile are geometry: 4-bank (BANK8=0) and the
 * smaller part size, which change DDRC_CFG/MMAP only (computed
 * below from the vendor encoder, validated against the T31 GOLD).
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

/*
 * Chip geometry. Standard T23/T23N use DDR2 M14D5121632A (64 MB:
 * row 13, col 10, 4-bank). T23DL/T23DN use M14D2561616A (32 MB:
 * row 13, col 9, 4-bank). Both are 16-bit, single CS0, DW32=0,
 * 4-bank (BANK8=0) - unlike T31's 128 MB M14D1G1664A which is
 * 8-bank. No T23 board is 128 MB.
 */
#define CONFIG_DDR_DW32		0
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0
#define DDR_BANK8		0

#if defined(CONFIG_T23_DRAM_32M)
#define DDR_ROW			13
#define DDR_COL			9
#define DDR_CHIP_0_SIZE		33554432	/* 32 MB */
#else
#define DDR_ROW			13
#define DDR_COL			10
#define DDR_CHIP_0_SIZE		67108864	/* 64 MB */
#endif

/*
 * Clock targets. The vendor standard T23N/T23DL profile is
 * APLL 1188 / MPLL 1200 / DDR 600 MHz (DDR_600M) - the SAME DDR
 * clock as T31, off the same MPLL 1200 (MNOD 100,1,2,1, set in
 * t23/pll.c). MPLL/DDR divider is /2 (cdr = 1), as on T31.
 */
#define DDR_MPLL_RATE		1200000000U
#define DDR_TARGET_RATE		600000000U

/*
 * DDRC_CFG / MMAP0 / MMAP1 are the exact vendor ddr_params_creator
 * output for the T23 geometry (CFG bitfields ROW0/COL0/BA0/CS/DW/
 * TYPE=DDR2/BSL/MISPE; MMAP base 0x20 << 8 | size mask). The
 * encoder was reproduced and validated bit-for-bit against the T31
 * 128 MB GOLD (0x0aa88a42 / 0x000020f8 / 0x00002800) before
 * deriving these.
 *
 * The CTRL / REFCNT / TIMINGn / MR0 set is the T31 600 MHz GOLD.
 * T23N runs DDR at the same 600 MHz off the same MPLL 1200, so
 * these are exact for T23N (not merely conservative). The 64/32
 * Mb parts also have tighter tRP/tRCD/tRFC than the 128 Mb part
 * the GOLD was tuned for, so the values have margin to spare.
 * HW-verified on the 64 MB T23N rig.
 */
#if defined(CONFIG_T23_DRAM_32M)
#define DDRC_CFG_VALUE		0x09288940	/* row13 col9 4-bank 32M */
#define DDRC_MMAP0_VALUE	0x000020fe
#define DDRC_MMAP1_VALUE	0x00002200
#else
#define DDRC_CFG_VALUE		0x0a288a40	/* row13 col10 4-bank 64M */
#define DDRC_MMAP0_VALUE	0x000020fc
#define DDRC_MMAP1_VALUE	0x00002400
#endif

#define DDRC_CTRL_VALUE		0x0000d91e
#define DDRC_REFCNT_VALUE	0x00910003
#define DDRC_TIMING1_VALUE	0x050f0a06
#define DDRC_TIMING2_VALUE	0x021c0a07
#define DDRC_TIMING3_VALUE	0x200a0722
#define DDRC_TIMING4_VALUE	0x26240031
#define DDRC_TIMING5_VALUE	0xff060405
#define DDRC_TIMING6_VALUE	0x321c0505
#define DDRP_MR0_VALUE		0x00000f73
#define DDR_CHIP_1_SIZE		0

#endif /* __T23_DDR_H__ */
