/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T31 (XBurst1) DDR2/DDR3 controller + Innophy PHY register map
 * and per-SKU variant table.
 *
 * Sibling of the XBurst2 driver (ddr_innophy.c) but a distinct IP: the
 * XBurst1 DDRC and Innophy PHY have a different register layout, so this
 * is its own driver/struct rather than a reuse. The per-SKU values
 * (geometry, clock setpoints, ddr_params_creator GOLD register words)
 * live in struct ingenic_t31_ddr_variant and are selected at runtime
 * from the devicetree via the node's compatible + the driver of_match
 * .data - no compile-time CONFIG_T31_VARIANT_*.
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
 * Per-SKU DDR configuration. One const instance per T31 variant
 * (ddr_t31_types.c), selected at probe time from the DT node compatible
 * via the driver of_match .data. All numeric fields are direct vendor
 * ddr_params_creator GOLD output (what the old mach/t31-ddr.h #if
 * branches produced) plus the SPL clock setpoints.
 */
struct ingenic_t31_ddr_variant {
	const char *name;		/* "T31N" - boot banner */
	const char *chip;		/* "M14D5121632A" - log only */
	enum ingenic_t31_ddr_type type;
	unsigned int cpu_mhz;		/* APLL/CPU clock, for the banner */

	/* Chip geometry (mem_remap inputs + reported size). */
	u8 row;
	u8 col;
	u8 bank8;			/* 1 = 8-bank, 0 = 4-bank */
	u8 cs0;
	u8 cs1;
	u8 dw32;			/* 0 = 16-bit data bus */
	u32 chip0_size;			/* bytes */

	/*
	 * SPL PLL setpoints: CPAPCR/CPMPCR M/N/OD encodings + the full
	 * CPCCR divider word (H0/H2/PCLK band tracks the MPLL rate).
	 * Consumed by t31/pll.c in SPL via ingenic_t31_ddr_pll_setpoints()
	 * before driver model is up.
	 */
	u32 apll_mnod;
	u32 mpll_mnod;
	u32 cpccr;

	/* DDR controller GOLD values (ddr_params_creator). */
	u32 ddrc_cfg;
	u32 ddrc_ctrl;
	u32 ddrc_mmap0;
	u32 ddrc_mmap1;
	u32 ddrc_refcnt;
	u32 ddrc_timing[6];		/* TIMING1..6 */
	u32 mr0;
};

struct ingenic_t31_ddr_priv {
	const struct ingenic_t31_ddr_variant *cfg;
	u32 ram_size;			/* total bytes */
};

/* Top-level DDR bring-up (ddr_t31.c), run once from the SPL probe. */
int ingenic_t31_ddr_sdram_init(const struct ingenic_t31_ddr_variant *cfg);

/*
 * SPL helper for t31/pll.c: find the T31 DDR node in the FDT (by trying
 * each known per-SKU compatible), and return that SKU's PLL setpoints.
 * Runs before driver model, so the caller must have set gd->fdt_blob
 * (via fdtdec_setup()). Returns 0 on success, negative on error.
 */
int ingenic_t31_ddr_pll_setpoints(u32 *apll_mnod, u32 *mpll_mnod, u32 *cpccr);

/* Per-SKU variant configs (ddr_t31_types.c). */
extern const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31x;
extern const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31n;
extern const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31l;
extern const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31lc;
extern const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31al;
extern const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31a;
extern const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_c100;

#endif /* _DRIVERS_RAM_INGENIC_DDR_T31_H */
