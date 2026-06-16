/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T31 (XBurst1) DDR2/DDR3 controller + Innophy PHY register map
 * and per-SKU parameter struct.
 *
 * Sibling of the XBurst2 driver (ddr_innophy.c) but a distinct IP: the
 * XBurst1 DDRC and Innophy PHY have a different register layout, so this
 * is its own driver/struct rather than a reuse. T31/T23/T21/T30 share it
 * (identical legacy DDRC + Innophy DDR2 IP); only the per-SKU values
 * (geometry, clock setpoints, ddr_params_creator GOLD register words)
 * differ. Those come from the &ddr node's "ingenic,sdram-params" u32 array
 * (struct ingenic_t31_ddr_params) under a single compatible
 * "ingenic,t31-ddr-innophy" - the mainline rk3328 DMC idiom, so the same
 * driver works under OF_CONTROL and, in the TPL, OF_PLATDATA. No
 * compile-time CONFIG_T31_VARIANT_*, no per-SKU compatible/.data table.
 *
 * Register map ported verbatim from the former
 * arch/mips/mach-xburst/include/mach/t31-ddr.h.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef _DRIVERS_RAM_INGENIC_DDR_T31_H
#define _DRIVERS_RAM_INGENIC_DDR_T31_H

#include <linux/types.h>

/* Base addresses (KSEG1 uncached); fixed on XBurst1 T31. */
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

/* CPM DDR clock-gate register (CPM_DDRCDR comes from <mach/t31.h>). */
#define CPM_DRCG		0xd0

/* DDR device type - selects the DDR2-soft vs legacy-DDR3 init branch. */
enum ingenic_t31_ddr_type {
	T31_DDR_TYPE_DDR2 = 0,
	T31_DDR_TYPE_DDR3,
};

/*
 * Per-SKU DDR configuration, deserialized from the &ddr node's
 * "ingenic,sdram-params" devicetree property (a flat u32 array). The field
 * order below IS the array order, and the struct is all-u32 so it can be
 * filled in one shot by casting it to (u32 *) - the mainline rk3328
 * rockchip,sdram-params idiom (dev_read_u32_array for OF_CONTROL,
 * dtoc-baked platdata for OF_PLATDATA in the TPL). All numeric fields are
 * direct vendor ddr_params_creator GOLD output (what the old mach/t31-ddr.h
 * #if branches produced) plus the SPL clock setpoints; T31/T23/T21/T30 all
 * share this driver, only the array values differ.
 *
 * [0..2] are the SPL PLL setpoints (CPAPCR/CPMPCR M/N/OD encodings + the
 * CPCCR divider word, whose H0/H2/PCLK band tracks the MPLL rate): the TPL's
 * UCLASS_RAM probe feeds them to t31/pll.c pll_init_params() before bringing
 * DDR up. [3] is the CPM_DDRCDR divider (DDR CK = MPLL / (ddr_cdr + 1));
 * 0 = default = 1 (MPLL/2) on every SKU except the T23N-LP 400 MHz profile
 * (MPLL 1200 / 3, ddr_cdr = 2). [4] selects the DDR2-soft vs legacy-DDR3
 * init branch (enum ingenic_t31_ddr_type).
 */
struct ingenic_t31_ddr_params {
	u32 apll_mnod;			/* [0]  CPAPCR M/N/OD (CPU/APLL) */
	u32 mpll_mnod;			/* [1]  CPMPCR M/N/OD (DDR/MPLL) */
	u32 cpccr;			/* [2]  CPCCR clock-divider word */
	u32 ddr_cdr;			/* [3]  CPM_DDRCDR divider (0 = MPLL/2) */
	u32 type;			/* [4]  enum ingenic_t31_ddr_type */
	u32 row;			/* [5]  chip geometry (mem_remap) */
	u32 col;			/* [6] */
	u32 bank8;			/* [7]  1 = 8-bank, 0 = 4-bank */
	u32 cs0;			/* [8] */
	u32 cs1;			/* [9] */
	u32 dw32;			/* [10] 0 = 16-bit data bus */
	u32 chip0_size;			/* [11] total DRAM bytes */
	u32 ddrc_cfg;			/* [12] DDRC GOLD values */
	u32 ddrc_ctrl;			/* [13] */
	u32 ddrc_mmap0;			/* [14] */
	u32 ddrc_mmap1;			/* [15] */
	u32 ddrc_refcnt;		/* [16] */
	u32 ddrc_timing[6];		/* [17..22] DDRC_TIMING1..6 */
	u32 mr0;			/* [23] */
};

/*
 * Imperative pre-DM DDR bring-up for the small-cache SoCs (T23): read the
 * per-SKU params from the &ddr node's "ingenic,sdram-params" FDT array, set the
 * PLLs and bring DRAM up. T23's 80 KB cache-as-RAM cannot run the DM scan
 * before DRAM, so it calls this from board_init_f before spl_init(); the later
 * UCLASS_RAM probe finds the one-shot guard set and only records the size.
 * The caller must have set gd->fdt_blob (fdtdec_setup()). 0 / -errno.
 */
int ingenic_t31_ddr_bringup_from_fdt(void);

#endif /* _DRIVERS_RAM_INGENIC_DDR_T31_H */
