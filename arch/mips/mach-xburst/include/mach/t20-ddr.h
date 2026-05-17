/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T20 DDR2 controller and Synopsys DWC PHY register map
 *
 * Profile: isvp_t20 (T20N) DDR2 M14D5121632A, 64 MB, 16-bit bus,
 * single CS0, 4-bank, DDR clock 500 MHz (MPLL 1000 / 2).
 *
 * T20 is the ODD ONE OUT of the XBurst1 camera SoCs: it does NOT use
 * the Innophy DDR2 core that T21/T23/T30/T31 share. T20 has the
 * Synopsys DesignWare (DWC) DDR PHY (the JZ4775/JZ4780-generation
 * controller, vendor arch/mips/cpu/xburst/ddr_dwc.c + t20/
 * ddr_set_dll.c). The DWC PHY has hardware impedance (ZQ)
 * calibration and a hardware DQS training engine, so the init
 * sequence is materially different from (and longer than) the
 * Innophy path - this is a fresh transliteration, not a mirror.
 *
 * All register VALUEs and the impedance/ODT/RZQ/remap tables are the
 * exact vendor ddr_params_creator output for the isvp_t20_sfcnor
 * build (CONFIG_DDR2_M14D5121632A, CONFIG_SYS_MEM_FREQ = MPLL/2 =
 * 500 MHz, DDR2). The host-compiled creator was validated by
 * reproducing the T31 128 MB GOLD bit-for-bit before trusting any
 * other SoC's output (see the T-series port memory).
 *
 * The PHY block sits at DDRC_BASE + DDR_PHY_OFFSET, where
 * DDR_PHY_OFFSET = (-0x4e0000 + 0x1000) (vendor asm/arch-t20/
 * base.h), i.e. 0xb34f0000 - 0x4df000 = 0xb3011000.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __T20_DDR_H__
#define __T20_DDR_H__

/* Base addresses (KSEG1 uncached) */
#define DDRC_BASE		0xb34f0000
#define DDR_PHY_BASE		0xb3011000	/* DDRC_BASE + DDR_PHY_OFFSET */

/* CPM offsets used by the DWC DDR path (DDR clock divider + DLL) */
#define CPM_DRCG		0xd0
/* CPM_DDRCDR (0x2c) comes from <mach/t20.h> */

/* DDR controller register offsets (relative to DDRC_BASE) */
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
#define DDRC_CFG_CS1EN		(1 << 7)
#define DDRC_CFG_CS0EN		(1 << 6)
#define DDRC_CTRL_ALH		(1 << 3)
#define DDRC_CTRL_CKE		(1 << 1)

/* DWC PHY register offsets (relative to DDR_PHY_BASE) */
#define DDRP_PIR		0x004	/* PHY Initialization Register */
#define DDRP_PGCR		0x008	/* PHY General Configuration */
#define DDRP_PGSR		0x00c	/* PHY General Status */
#define DDRP_DLLGCR		0x010	/* DLL General Control */
#define DDRP_ACDLLCR		0x014	/* AC DLL Control */
#define DDRP_PTR0		0x018	/* PHY Timing Register 0 */
#define DDRP_PTR1		0x01c	/* PHY Timing Register 1 */
#define DDRP_PTR2		0x020	/* PHY Timing Register 2 */
#define DDRP_ACIOCR		0x024	/* AC I/O Configuration */
#define DDRP_DXCCR		0x028	/* DATX8 Common Configuration */
#define DDRP_DSGCR		0x02c	/* DDR System General Config */
#define DDRP_DCR		0x030	/* DRAM Configuration */
#define DDRP_DTPR0		0x034	/* DRAM Timing Parameters 0 */
#define DDRP_DTPR1		0x038	/* DRAM Timing Parameters 1 */
#define DDRP_DTPR2		0x03c	/* DRAM Timing Parameters 2 */
#define DDRP_MR0		0x040	/* Mode Register 0 */
#define DDRP_MR1		0x044	/* Mode Register 1 */
#define DDRP_MR2		0x048	/* Mode Register 2 */
#define DDRP_MR3		0x04c	/* Mode Register 3 */
#define DDRP_ODTCR		0x050	/* ODT Configure Register */
#define DDRP_DTAR		0x054	/* Data Training Address */
#define DDRP_DXGCR(n)		(0x1c0 + (n) * 0x40)	/* DATX8 n Gen Cfg */
#define DDRP_DXGSR0(n)		(0x1c4 + (n) * 0x40)	/* DATX8 n Gen Sts */
#define DDRP_ZQXCR0(n)		(0x180 + (n) * 0x10)	/* ZQ Imp Ctrl 0 */
#define DDRP_ZQXCR1(n)		(0x184 + (n) * 0x10)	/* ZQ Imp Ctrl 1 */
#define DDRP_ZQXSR0(n)		(0x188 + (n) * 0x10)	/* ZQ Imp Status 0 */

/* DDRP PHY Initialization Register bits */
#define DDRP_PIR_INIT		(1 << 0)
#define DDRP_PIR_DLLSRST	(1 << 1)
#define DDRP_PIR_DLLLOCK	(1 << 2)
#define DDRP_PIR_ZCAL		(1 << 3)
#define DDRP_PIR_ITMSRST	(1 << 4)
#define DDRP_PIR_DRAMRST	(1 << 5)
#define DDRP_PIR_DRAMINT	(1 << 6)
#define DDRP_PIR_QSTRN		(1 << 7)
#define DDRP_PIR_DLLBYP		(1 << 17)
#define DDRP_PIR_LOCKBYP	(1 << 29)

/* DDRP PHY General Status Register bits */
#define DDRP_PGSR_IDONE		(1 << 0)
#define DDRP_PGSR_DLDONE	(1 << 1)
#define DDRP_PGSR_ZCDONE	(1 << 2)
#define DDRP_PGSR_DIDONE	(1 << 3)
#define DDRP_PGSR_DTDONE	(1 << 4)
#define DDRP_PGSR_DTERR		(1 << 5)
#define DDRP_PGSR_DTIERR	(1 << 6)

#define DDRP_ZQXCR_ZDEN		(1 << 28)
#define DDRP_DXGCR_DXEN		(1 << 0)

/*
 * GOLD register values: exact vendor ddr_params_creator output for
 * isvp_t20_sfcnor (T20N, M14D5121632A, DDR2, 500 MHz). Tool
 * validated bit-for-bit against the T31 128 MB GOLD.
 */
#define DDRC_CFG_VALUE		0x0a688a40
#define DDRC_CTRL_VALUE		0x0000d91e
#define DDRC_MMAP0_VALUE	0x000020fc
#define DDRC_MMAP1_VALUE	0x00002400
#define DDRC_REFCNT_VALUE	0x00f20001
#define DDRC_TIMING1_VALUE	0x040e0806
#define DDRC_TIMING2_VALUE	0x02170707
#define DDRC_TIMING3_VALUE	0x2007051e
#define DDRC_TIMING4_VALUE	0x1a240031
#define DDRC_TIMING5_VALUE	0xff060505
#define DDRC_TIMING6_VALUE	0x32170505
#define DDRC_AUTOSR_EN_VALUE	0x00000000
#define DDRP_DCR_VALUE		0x00000002
#define DDRP_MR0_VALUE		0x00000e73
#define DDRP_MR1_VALUE		0x00000002
#define DDRP_MR2_VALUE		0x00000000
#define DDRP_MR3_VALUE		0x00000000
#define DDRP_PTR0_VALUE		0x00228019
#define DDRP_PTR1_VALUE		0x064186a0
#define DDRP_PTR2_VALUE		0x00000000
#define DDRP_DTPR0_VALUE	0x3cb7779a
#define DDRP_DTPR1_VALUE	0x003500b8
#define DDRP_DTPR2_VALUE	0x1001a8c8
#define DDRP_PGCR_VALUE		0x01842e02
#define DDRP_ODTCR_VALUE	0x00000000
#define DDRP_DX0GCR_VALUE	0x00090881
#define DDRP_DX1GCR_VALUE	0x00090881
#define DDRP_DX2GCR_VALUE	0x00090e80
#define DDRP_DX3GCR_VALUE	0x00090e80
#define DDRP_ZQNCR1_VALUE	0x0000006b

/* {0:cal_value, 1:req_value} for the ZQ impedance recompute */
#define DDRP_IMPANDCE_ARRAY	{ 0x00009c40, 0x00009c40 }
#define DDRP_ODT_IMPANDCE_ARRAY	{ 0x0000c350, 0x0000c350 }
#define DDRP_RZQ_TABLE \
	{ 0x00, 0x01, 0x02, 0x03, 0x06, 0x07, 0x04, 0x05, \
	  0x0c, 0x0d, 0x0e, 0x0f, 0x0a, 0x0b, 0x08, 0x09, \
	  0x18, 0x19, 0x1a, 0x1b, 0x1e, 0x1f, 0x1c, 0x1d, \
	  0x14, 0x15, 0x16, 0x17, 0x12, 0x13, 0x10, 0x11 }

/*
 * Address-remap table. Unlike the Innophy path (which computes the
 * swap from row/col), the DWC driver writes these precomputed words
 * straight to DDRC_REMAP(1..5).
 */
#define REMMAP_ARRAY \
	{ 0x03020d0c, 0x07060504, 0x0b0a0908, 0x0f0e0100, 0x13121110 }

/* Clock targets: T20N MPLL 1000 MHz, DDR 500 MHz (cdr = 1, /2). */
#define DDR_MPLL_RATE		1000000000U
#define DDR_TARGET_RATE		500000000U

#define DDR_CHIP_0_SIZE		67108864	/* 64 MB */
#define DDR_CHIP_1_SIZE		0
#define T20_DRAM_SIZE		0x04000000u	/* 64 MB */

#endif /* __T20_DDR_H__ */
