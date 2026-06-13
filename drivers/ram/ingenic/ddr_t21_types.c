// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T21 (XBurst1) DDR2 - per-SKU register/clock tables.
 *
 * T21 uses the SAME legacy XBurst1 DDRC + Innophy DDR2 PHY as T31/T23,
 * so it reuses the ddr_t31.c driver (struct ingenic_t31_ddr_variant,
 * of_match .data) - only the per-SKU clock values differ. These replace
 * the former compile-time CONFIG_T21_VARIANT_* / #if branches in
 * arch/mips/mach-xburst/include/mach/t21-ddr.h; the numeric values are
 * the same verbatim vendor ddr_params_creator GOLD output.
 *
 * Both T21 SKUs populate the 64 MB DDR2 M14D5121632A (row 13 / col 10,
 * 4-bank, 16-bit) - the same part as T23/T23N; no T21 board is 128 or
 * 32 MB. The 500 MHz timing set is bit-identical to the T23X DDR_500M
 * GOLD except MR0: T21 is NOT in the vendor DDR2_CHIP_MR0_DLL_RST list
 * (isvp_common.h), so it takes the #else MR0 path (0x0c73/0x0e73, not
 * the T23/T31 0x0f73).
 *
 * Clocks (CPAPCR/CPMPCR are the exact vendor CONFIG_SYS_*_MNOD words,
 * non-uniform N/OD, taken verbatim; CPCCR = PCLK 8 / H2 4 / H0 4 for
 * both SKUs, the vendor DDR_450M/500M profile - same word as the T23
 * MPLL-1000 band):
 *   T21N     APLL  864 / MPLL  900 / DDR 450 (MPLL/2)
 *   T21-HP   APLL 1200 / MPLL 1000 / DDR 500 (MPLL/2)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <linux/types.h>
#include "ddr_t31.h"

/* Exact vendor CPAPCR/CPMPCR words (non-uniform N/OD; verbatim). */
#define T21_APLL_864	0x04704840u
#define T21_APLL_1200	0x03100860u
#define T21_MPLL_900	0x04a04840u
#define T21_MPLL_1000	0x07c08820u

/*
 * CPCCR: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL, PCLK 8 / H2 4 / H0 4 /
 * L2 2 / CPU 1 - the vendor DDR_450M/500M divider profile, same word
 * as T31/T23's MPLL-1000 band.
 */
#define T21_CPCCR		0x9a073310u

#define T21_DDRC_CTRL_VALUE	0x0000d91e	/* == T31/T23 */

/* ---- DDR2 64 MB M14D5121632A geometry (clock-invariant part) ---- */
#define T21_DDR2_64M \
	.type		= T31_DDR_TYPE_DDR2, \
	.chip		= "M14D5121632A", \
	.row		= 13, .col = 10, .bank8 = 0, .cs0 = 1, .cs1 = 0, \
	.dw32		= 0, \
	.chip0_size	= 0x04000000, \
	.ddrc_cfg	= 0x0a288a40, \
	.ddrc_ctrl	= T21_DDRC_CTRL_VALUE, \
	.ddrc_mmap0	= 0x000020fc, \
	.ddrc_mmap1	= 0x00002400

/* T21N: APLL 864 / MPLL 900 / DDR 450 (vendor isvp_t21 default). */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t21n = {
	.name		= "T21N",
	.cpu_mhz	= 864,
	T21_DDR2_64M,
	.apll_mnod	= T21_APLL_864,
	.mpll_mnod	= T21_MPLL_900,
	.cpccr		= T21_CPCCR,
	.ddr_cdr	= 0,			/* MPLL / 2 */
	.ddrc_refcnt	= 0x00da0001,
	.ddrc_timing	= { 0x040e0706, 0x02150607, 0x2006051b, 0x17240031,
			    0xff060405, 0x32150505 },
	.mr0		= 0x00000c73,
};

/* T21 HIGH_PERF: APLL 1200 / MPLL 1000 / DDR 500. */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t21hp = {
	.name		= "T21-HP",
	.cpu_mhz	= 1200,
	T21_DDR2_64M,
	.apll_mnod	= T21_APLL_1200,
	.mpll_mnod	= T21_MPLL_1000,
	.cpccr		= T21_CPCCR,
	.ddr_cdr	= 0,			/* MPLL / 2 */
	.ddrc_refcnt	= 0x00f20001,
	.ddrc_timing	= { 0x040e0806, 0x02170707, 0x2007051e, 0x1a240031,
			    0xff060405, 0x32170505 },
	.mr0		= 0x00000e73,
};
