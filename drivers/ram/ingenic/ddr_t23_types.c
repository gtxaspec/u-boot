// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T23 (XBurst1) DDR2 - per-SKU register/clock tables.
 *
 * T23 uses the SAME legacy XBurst1 DDRC + Innophy DDR2 PHY as T31, so
 * it reuses the ddr_t31.c driver (struct ingenic_t31_ddr_variant,
 * of_match .data) - only the per-SKU geometry and clock values differ.
 * These replace the former compile-time CONFIG_T23_VARIANT_* / #if
 * branches in arch/mips/mach-xburst/include/mach/t23-ddr.h; the numeric
 * values are the same verbatim vendor ddr_params_creator GOLD output.
 *
 * No T23 board is 128 MB: T23/T23N/T23X use the 64 MB DDR2
 * M14D5121632A (4-bank), T23DL/T23DN the 32 MB M14D2561616A. The
 * 600 MHz timing set IS the T31 600 MHz GOLD (same MPLL 1200 / DDR
 * 600, same PHY); only the 4-bank geometry (cfg/mmap) differs. The
 * 500 MHz (MPLL 1000) and 400 MHz (MPLL 1200 / 3, ddr_cdr = 2) sets
 * are the vendor DDR_500M / DDR_400M output.
 *
 * Clocks (CPAPCR/CPMPCR are the exact vendor CONFIG_SYS_*_MNOD words,
 * non-uniform N/OD - NOT the uniform T31 M/N/OD scheme; CPCCR words
 * match T31's MPLL-band dividers, verified bit-for-bit):
 *   T23N/ZN/DL  APLL 1188 / MPLL 1200 / DDR 600
 *   T23N-HP     APLL 1400 / MPLL 1200 / DDR 600
 *   T23N-LP     APLL  936 / MPLL 1200 / DDR 400 (ddr_cdr = 2)
 *   T23X/DN     APLL 1000 / MPLL 1000 / DDR 500
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <linux/types.h>
#include "ddr_t31.h"

/* Exact vendor CPAPCR/CPMPCR words (non-uniform N/OD; verbatim). */
#define T23_APLL_1188	0x1290d100u
#define T23_APLL_1400	0x15e0d100u
#define T23_APLL_936	0x0ea0d100u
#define T23_PLL_1000	0x07d05900u	/* APLL == MPLL on the 500 profile */
#define T23_MPLL_1200	0x06405100u
#define T23_MPLL_1000	0x07d05900u

/* CPCCR words == T31's MPLL-band dividers (see ddr_t31_types.c). */
#define T23_CPCCR_MPLL1200	0x9a0b5510u	/* PCLK 12 / H2 6 / H0 6 */
#define T23_CPCCR_MPLL1000	0x9a073310u	/* PCLK 8 / H2 4 / H0 4 */

#define T23_DDRC_CTRL_VALUE	0x0000d91e	/* == T31 */

/* ---- DDR2 64 MB M14D5121632A, 4-bank, DDR 600 (== T31 600 GOLD) ---- */
#define T23_DDR2_600_64M \
	.type		= T31_DDR_TYPE_DDR2, \
	.chip		= "M14D5121632A", \
	.row		= 13, .col = 10, .bank8 = 0, .cs0 = 1, .cs1 = 0, \
	.dw32		= 0, \
	.chip0_size	= 0x04000000, \
	.ddrc_cfg	= 0x0a288a40, \
	.ddrc_ctrl	= T23_DDRC_CTRL_VALUE, \
	.ddrc_mmap0	= 0x000020fc, \
	.ddrc_mmap1	= 0x00002400, \
	.ddrc_refcnt	= 0x00910003, \
	.ddrc_timing	= { 0x050f0a06, 0x021c0a07, 0x200a0722, 0x26240031, \
			    0xff060405, 0x321c0505 }, \
	.mr0		= 0x00000f73

/* ---- DDR2 32 MB M14D2561616A, 4-bank, DDR 600 ---- */
#define T23_DDR2_600_32M \
	.type		= T31_DDR_TYPE_DDR2, \
	.chip		= "M14D2561616A", \
	.row		= 13, .col = 9, .bank8 = 0, .cs0 = 1, .cs1 = 0, \
	.dw32		= 0, \
	.chip0_size	= 0x02000000, \
	.ddrc_cfg	= 0x09288940, \
	.ddrc_ctrl	= T23_DDRC_CTRL_VALUE, \
	.ddrc_mmap0	= 0x000020fe, \
	.ddrc_mmap1	= 0x00002200, \
	.ddrc_refcnt	= 0x00910003, \
	.ddrc_timing	= { 0x050f0a06, 0x021c0a07, 0x200a0722, 0x26240031, \
			    0xff060405, 0x321c0505 }, \
	.mr0		= 0x00000f73

/* ---- DDR2 64 MB, DDR 500 (MPLL 1000 / 2): T23X ---- */
#define T23_DDR2_500_64M \
	.type		= T31_DDR_TYPE_DDR2, \
	.chip		= "M14D5121632A", \
	.row		= 13, .col = 10, .bank8 = 0, .cs0 = 1, .cs1 = 0, \
	.dw32		= 0, \
	.chip0_size	= 0x04000000, \
	.ddrc_cfg	= 0x0a288a40, \
	.ddrc_ctrl	= T23_DDRC_CTRL_VALUE, \
	.ddrc_mmap0	= 0x000020fc, \
	.ddrc_mmap1	= 0x00002400, \
	.ddrc_refcnt	= 0x00f20001, \
	.ddrc_timing	= { 0x040e0806, 0x02170707, 0x2007051e, 0x1a240031, \
			    0xff060405, 0x32170505 }, \
	.mr0		= 0x00000f73

/* ---- DDR2 32 MB, DDR 500 (MPLL 1000 / 2): T23DN ---- */
#define T23_DDR2_500_32M \
	.type		= T31_DDR_TYPE_DDR2, \
	.chip		= "M14D2561616A", \
	.row		= 13, .col = 9, .bank8 = 0, .cs0 = 1, .cs1 = 0, \
	.dw32		= 0, \
	.chip0_size	= 0x02000000, \
	.ddrc_cfg	= 0x09288940, \
	.ddrc_ctrl	= T23_DDRC_CTRL_VALUE, \
	.ddrc_mmap0	= 0x000020fe, \
	.ddrc_mmap1	= 0x00002200, \
	.ddrc_refcnt	= 0x00f20001, \
	.ddrc_timing	= { 0x040e0806, 0x02170707, 0x2007041e, 0x12240031, \
			    0xff060405, 0x32170505 }, \
	.mr0		= 0x00000f73

/* ---- DDR2 64 MB, DDR 400 (MPLL 1200 / 3, ddr_cdr = 2): T23N-LP ---- */
#define T23_DDR2_400_64M \
	.type		= T31_DDR_TYPE_DDR2, \
	.chip		= "M14D5121632A", \
	.row		= 13, .col = 10, .bank8 = 0, .cs0 = 1, .cs1 = 0, \
	.dw32		= 0, \
	.chip0_size	= 0x04000000, \
	.ddrc_cfg	= 0x0a288a40, \
	.ddrc_ctrl	= T23_DDRC_CTRL_VALUE, \
	.ddrc_mmap0	= 0x000020fc, \
	.ddrc_mmap1	= 0x00002400, \
	.ddrc_refcnt	= 0x00c20001, \
	.ddrc_timing	= { 0x040d0606, 0x02120607, 0x20060418, 0x14240031, \
			    0xff060405, 0x32120505 }, \
	.mr0		= 0x00000b73, \
	.ddr_cdr	= 2

/* ----------------------- 600 MHz SKUs ----------------------- */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t23n = {
	.name		= "T23N",
	.cpu_mhz	= 1188,
	.apll_mnod	= T23_APLL_1188,
	.mpll_mnod	= T23_MPLL_1200,
	.cpccr		= T23_CPCCR_MPLL1200,
	T23_DDR2_600_64M,
};

const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t23dl = {
	.name		= "T23DL",
	.cpu_mhz	= 1188,
	.apll_mnod	= T23_APLL_1188,
	.mpll_mnod	= T23_MPLL_1200,
	.cpccr		= T23_CPCCR_MPLL1200,
	T23_DDR2_600_32M,
};

const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t23n_hp = {
	.name		= "T23N-HP",
	.cpu_mhz	= 1400,
	.apll_mnod	= T23_APLL_1400,
	.mpll_mnod	= T23_MPLL_1200,
	.cpccr		= T23_CPCCR_MPLL1200,
	T23_DDR2_600_64M,
};

/* ----------------------- 400 MHz SKU ----------------------- */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t23n_lp = {
	.name		= "T23N-LP",
	.cpu_mhz	= 936,
	.apll_mnod	= T23_APLL_936,
	.mpll_mnod	= T23_MPLL_1200,
	.cpccr		= T23_CPCCR_MPLL1200,
	T23_DDR2_400_64M,
};

/* ----------------------- 500 MHz SKUs ----------------------- */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t23x = {
	.name		= "T23X",
	.cpu_mhz	= 1000,
	.apll_mnod	= T23_PLL_1000,
	.mpll_mnod	= T23_MPLL_1000,
	.cpccr		= T23_CPCCR_MPLL1000,
	T23_DDR2_500_64M,
};

const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t23dn = {
	.name		= "T23DN",
	.cpu_mhz	= 1000,
	.apll_mnod	= T23_PLL_1000,
	.mpll_mnod	= T23_MPLL_1000,
	.cpccr		= T23_CPCCR_MPLL1000,
	T23_DDR2_500_32M,
};
