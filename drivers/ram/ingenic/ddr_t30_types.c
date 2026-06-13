// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T30 (XBurst1) DDR2 - per-SKU register/clock tables.
 *
 * T30 uses the SAME legacy XBurst1 DDRC + Innophy DDR2 PHY as
 * T31/T23/T21, so it reuses the ddr_t31.c driver (struct
 * ingenic_t31_ddr_variant, of_match .data) - only the per-SKU variant
 * table is added. These replace the former compile-time
 * CONFIG_T30_VARIANT_* / #if branches in mach/t30-ddr.h; the numeric
 * values are the same verbatim vendor ddr_params_creator GOLD output.
 *
 * All SKUs run DDR 500 MHz (MPLL 1000 / 2); they differ in the APLL
 * (CPU) clock and the DRAM part: T30N/T30L use the 64 MB DDR2
 * M14D5121632A (4-bank), T30X/T30A the 128 MB M14D1G1664A (8-bank).
 * T30A is param-identical to T30X (alias compatible, shared struct).
 * MR0 = 0x0e73: T30 IS in the vendor DDR2_CHIP_MR0_DLL_RST list
 * (T23||T30||T31||C100); the CL nibble tracks the DDR clock
 * (T21@450 0x0c73, T30@500 0x0e73, T23@600 0x0f73).
 *
 * Clocks (CPAPCR/CPMPCR exact ORIGINAL vendor isvp_t30.h words - the
 * ingenic-u-boot-xburst1 fork's 900/750 values are wrong for T30;
 * OD0 in the [7:5] field, taken verbatim):
 *   T30N/T30X/T30A  APLL 864 / MPLL 1000 / DDR 500
 *   T30L            APLL 720 / MPLL 1000 / DDR 500
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <linux/types.h>
#include "ddr_t31.h"

/* Exact vendor CPAPCR/CPMPCR words (verbatim). */
#define T30_APLL_864	0x04704840u
#define T30_APLL_720	0x07705040u
#define T30_MPLL_1000	0x07c08820u

/*
 * CPCCR: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL, PCLK 8 / H2 4 / H0 4 /
 * L2 2 / CPU 1 - the vendor DDR_500M divider profile, same word as
 * the T21/T23 MPLL-1000 band.
 */
#define T30_CPCCR		0x9a073310u

#define T30_DDRC_CTRL_VALUE	0x0000d91e	/* == T31/T23/T21 */

/* ---- DDR2 64 MB M14D5121632A, 4-bank, DDR 500 ---- */
#define T30_DDR2_500_64M \
	.type		= T31_DDR_TYPE_DDR2, \
	.chip		= "M14D5121632A", \
	.row		= 13, .col = 10, .bank8 = 0, .cs0 = 1, .cs1 = 0, \
	.dw32		= 0, \
	.chip0_size	= 0x04000000, \
	.ddrc_cfg	= 0x0a288a40, \
	.ddrc_ctrl	= T30_DDRC_CTRL_VALUE, \
	.ddrc_mmap0	= 0x000020fc, \
	.ddrc_mmap1	= 0x00002400, \
	.ddrc_refcnt	= 0x00f20001, \
	.ddrc_timing	= { 0x040e0806, 0x02170707, 0x2007051e, 0x1a240031, \
			    0xff060405, 0x32170505 }, \
	.mr0		= 0x00000e73

/* ---- DDR2 128 MB M14D1G1664A, 8-bank, DDR 500 ---- */
#define T30_DDR2_500_128M \
	.type		= T31_DDR_TYPE_DDR2, \
	.chip		= "M14D1G1664A", \
	.row		= 13, .col = 10, .bank8 = 1, .cs0 = 1, .cs1 = 0, \
	.dw32		= 0, \
	.chip0_size	= 0x08000000, \
	.ddrc_cfg	= 0x0aa88a42, \
	.ddrc_ctrl	= T30_DDRC_CTRL_VALUE, \
	.ddrc_mmap0	= 0x000020f8, \
	.ddrc_mmap1	= 0x00002800, \
	.ddrc_refcnt	= 0x00f20001, \
	.ddrc_timing	= { 0x040e0806, 0x02170807, 0x2008051d, 0x1f240031, \
			    0xff060405, 0x32170505 }, \
	.mr0		= 0x00000e73

/* T30N: APLL 864, 64 MB. */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t30n = {
	.name		= "T30N",
	.cpu_mhz	= 864,
	T30_DDR2_500_64M,
	.apll_mnod	= T30_APLL_864,
	.mpll_mnod	= T30_MPLL_1000,
	.cpccr		= T30_CPCCR,
	.ddr_cdr	= 0,			/* MPLL / 2 */
};

/* T30L / lite: APLL 720, 64 MB (vendor isvp_t30 default). */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t30l = {
	.name		= "T30L",
	.cpu_mhz	= 720,
	T30_DDR2_500_64M,
	.apll_mnod	= T30_APLL_720,
	.mpll_mnod	= T30_MPLL_1000,
	.cpccr		= T30_CPCCR,
	.ddr_cdr	= 0,			/* MPLL / 2 */
};

/* T30X: APLL 864, 128 MB. T30A is param-identical (alias compatible). */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t30x = {
	.name		= "T30X",
	.cpu_mhz	= 864,
	T30_DDR2_500_128M,
	.apll_mnod	= T30_APLL_864,
	.mpll_mnod	= T30_MPLL_1000,
	.cpccr		= T30_CPCCR,
	.ddr_cdr	= 0,			/* MPLL / 2 */
};
