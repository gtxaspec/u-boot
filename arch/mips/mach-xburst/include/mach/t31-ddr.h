/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T31 DDR2/DDR3 controller and Innophy PHY register map
 *
 * Profile: T31 DDR @ 600 MHz (MPLL 1200 / 2), 16-bit, CS0 only.
 *   CONFIG_T31_DDR3      = M15T1G1664A DDR3 128 MB, 8-bank
 *     (T31A and C100); legacy Innophy DDR3 init path. Params
 *     from isvp_c100_sfcnor (DDR3 @ DDR_600M = our fixed clock).
 *   CONFIG_T31_DRAM_128M = M14D1G1664A DDR2 128 MB, 8-bank
 *     (isvp_t31_sfcnor_ddr128M - T31X/T31AL).
 *   else                 = M14D5121632A DDR2 64 MB, 4-bank
 *     (isvp_t31_sfcnor - T31N/T31L/T31LC).
 *   All run the SAME 600 MHz clock; the 128 MB DDR2/DDR3 parts
 *   share row/col/bank/size geometry. DDR3 differs only in the
 *   clock/type-dependent CFG/REFCNT/TIMING1-6/MR0 set and the
 *   Innophy init branches in sdram.c. GOLD values are the
 *   known-good vendor host-params-creator output.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __T31_DDR_H__
#define __T31_DDR_H__

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
#define T31_DQS_DELAY_L		(DDR_PHY_BASE + 0x120)
#define T31_DQS_DELAY_H		(DDR_PHY_BASE + 0x160)
#define T31_REG46		(DDR_PHY_BASE + 0x118)
#define T31_REG56		(DDR_PHY_BASE + 0x158)
#define T31_INIT_COMP		(DDR_PHY_BASE + 0xd0)

#define DDR_APB_PHY_INIT	(DDR_APB_BASE + 0x8c)
#define REG_DDR_CTRL		(DDRC_BASE + 0x008)
#define REG_DDR_CFG		(DDRC_BASE + 0x004)
#define REG_DDR_LMR		(DDRC_BASE + 0x00c)

/* CPM registers used for the DDR clock divider and DLL reset */
#define CPM_DRCG		0xd0
#define CPM_DDRCDR		0x2c

/*
 * Chip geometry. Both parts are row 13 / col 10 (DW32=0); 128M
 * M14D1G1664A is 8-bank, 64M M14D5121632A is 4-bank.
 */
#define DDR_ROW			13
#define DDR_COL			10
#if defined(CONFIG_T31_DRAM_128M)
#define DDR_BANK8		1	/* M14D1G1664A 128 MB, 8-bank */
#else
#define DDR_BANK8		0	/* M14D5121632A 64 MB, 4-bank */
#endif
#define CONFIG_DDR_DW32		0
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0

/* Clock targets for this profile */
#define DDR_MPLL_RATE		1200000000U
#define DDR_TARGET_RATE		600000000U

/*
 * GOLD computed register values from the vendor build that produced the
 * working SPL (CONFIG_DDR_HOST_CC + CONFIG_DDR_PARAMS_CREATOR). The
 * innophy DDR2 path uses these plus DDRP_MR0_VALUE and the hardcoded
 * PHY pokes; the DWC DDRP_* values in the vendor generated header are
 * for the other PHY and are not used here.
 */
#if defined(CONFIG_T31_DDR3)
#define DDRC_CFG_VALUE		0x0aac8a42	/* M15T1G1664A DDR3 128M 8-bank */
#define DDRC_MMAP0_VALUE	0x000020f8
#define DDRC_MMAP1_VALUE	0x00002800
#elif defined(CONFIG_T31_DRAM_128M)
#define DDRC_CFG_VALUE		0x0aa88a42	/* M14D1G1664A 128M 8-bank */
#define DDRC_MMAP0_VALUE	0x000020f8
#define DDRC_MMAP1_VALUE	0x00002800
#else
#define DDRC_CFG_VALUE		0x0a288a40	/* M14D5121632A 64M 4-bank */
#define DDRC_MMAP0_VALUE	0x000020fc
#define DDRC_MMAP1_VALUE	0x00002400
#endif
#define DDRC_CTRL_VALUE		0x0000d91e
/*
 * REFCNT/TIMING/MR0 are the 600 MHz set - clock-dependent only,
 * geometry-independent (confirmed on T30: 64M and 128M @ the same
 * clock share these). T31 always runs DDR 600 (MPLL 1200/2), so
 * both the 64M and 128M parts use this one set.
 */
#if defined(CONFIG_T31_DDR3)
/*
 * M15T1G1664A DDR3 @ 600 MHz - the host ddr_params_creator set
 * for isvp_c100_sfcnor (CONFIG_C100,DDR3_128M, vendor DDR_600M),
 * verbatim. T31 always runs the DDR clock at 600 (MPLL 1200/2),
 * so both T31A and C100 (same M15T1G1664A part) use this single
 * DDR3-@600 set. (NOT the isvp_t31a vendor config, whose DDR_750M
 * timing would be wrong against our fixed 600 MHz DDR clock.)
 */
#define DDRC_REFCNT_VALUE	0x00910003
#define DDRC_TIMING1_VALUE	0x050f0a06
#define DDRC_TIMING2_VALUE	0x04170908
#define DDRC_TIMING3_VALUE	0x2109051f
#define DDRC_TIMING4_VALUE	0x30250043
#define DDRC_TIMING5_VALUE	0xff080505
#define DDRC_TIMING6_VALUE	0x801c0505
#define DDRP_MR0_VALUE		0x00001a40
#else
#define DDRC_REFCNT_VALUE	0x00910003
#define DDRC_TIMING1_VALUE	0x050f0a06
#define DDRC_TIMING2_VALUE	0x021c0a07
#define DDRC_TIMING3_VALUE	0x200a0722
#define DDRC_TIMING4_VALUE	0x26240031
#define DDRC_TIMING5_VALUE	0xff060405
#define DDRC_TIMING6_VALUE	0x321c0505
#define DDRP_MR0_VALUE		0x00000f73
#endif

#if defined(CONFIG_T31_DRAM_128M)
#define DDR_CHIP_0_SIZE		134217728	/* 128 MB */
#else
#define DDR_CHIP_0_SIZE		67108864	/* 64 MB */
#endif
#define DDR_CHIP_1_SIZE		0

#endif /* __T31_DDR_H__ */
