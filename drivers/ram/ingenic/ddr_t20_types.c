// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T20 (XBurst1, Synopsys DWC PHY) DDR2 - per-SKU register/clock
 * tables.
 *
 * T20 is the only XBurst1 camera SoC that is NOT Innophy, so it has its
 * own driver (ddr_t20.c, struct ingenic_t20_ddr_variant) rather than
 * reusing ddr_t31.c. These instances replace the former compile-time
 * CONFIG_T20_VARIANT_* / #if branches in mach/t20-ddr.h; the numeric
 * values are the same verbatim vendor ddr_params_creator GOLD output.
 *
 * All three SKUs run DDR 500 MHz (MPLL 1000 / 2, cdr = 1); they differ in
 * the APLL (CPU) clock and the DRAM part:
 *   T20N  APLL 860 / 64 MB  M14D5121632A (4-bank)  - isvp_t20 default
 *   T20L  APLL 712 / 64 MB  M14D5121632A (4-bank)
 *   T20X  APLL 860 / 128 MB M14D1G1664A  (8-bank)
 * MPLL (1000) and the CPCCR DDR_500M divider word are identical on every
 * SKU; only APLL and the 64M-vs-128M DWC geometry/timing set differ.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <linux/types.h>
#include "ddr_t20.h"

/* Exact vendor CPAPCR/CPMPCR words (M/N/OD1/OD0, no PLLRG; verbatim). */
#define T20_APLL_860	0x04708900u
#define T20_APLL_712	0x03b08900u
#define T20_MPLL_1000	((125u << 20) | (3u << 14) | (1u << 11) | (1u << 8))

/*
 * CPCCR: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL, PCLK 8 / H2 4 / H0 4 /
 * L2 2 / CPU 1 - the vendor DDR_500M divider profile (same word as
 * T21/T23/T30/T31's MPLL-1000 band).
 */
#define T20_CPCCR	0x9a073310u

/* ---- DDR2 64 MB M14D5121632A, 4-bank, DDR 500 (T20N/T20L) ---- */
#define T20_DDR2_500_64M \
	.chip		= "M14D5121632A", \
	.bank8		= 0, \
	.chip0_size	= 0x04000000, \
	.ddrc_cfg	= 0x0a688a40, \
	.ddrc_mmap0	= 0x000020fc, \
	.ddrc_mmap1	= 0x00002400, \
	.ddrc_timing	= { 0x040e0806, 0x02170707, 0x2007051e, 0x1a240031, \
			    0xff060505, 0x32170505 }, \
	.ddrp_dcr	= 0x00000002, \
	.ddrp_dtpr0	= 0x3cb7779a, \
	.ddrp_dtpr1	= 0x003500b8, \
	.remap		= { 0x03020d0c, 0x07060504, 0x0b0a0908, 0x0f0e0100, \
			    0x13121110 }

/* ---- DDR2 128 MB M14D1G1664A, 8-bank, DDR 500 (T20X) ---- */
#define T20_DDR2_500_128M \
	.chip		= "M14D1G1664A", \
	.bank8		= 1, \
	.chip0_size	= 0x08000000, \
	.ddrc_cfg	= 0x0ae88a42, \
	.ddrc_mmap0	= 0x000020f8, \
	.ddrc_mmap1	= 0x00002800, \
	.ddrc_timing	= { 0x040e0806, 0x02170807, 0x2008051d, 0x1f240031, \
			    0xff060505, 0x32170505 }, \
	.ddrp_dcr	= 0x0000000a, \
	.ddrp_dtpr0	= 0x3ab7889a, \
	.ddrp_dtpr1	= 0x004000b8, \
	.remap		= { 0x030e0d0c, 0x07060504, 0x0b0a0908, 0x0f020100, \
			    0x13121110 }

/* T20N: APLL 860, 64 MB (isvp_t20 default). */
const struct ingenic_t20_ddr_variant ingenic_t20_ddr_variant_t20n = {
	.name		= "T20N",
	.cpu_mhz	= 860,
	T20_DDR2_500_64M,
	.apll_mnod	= T20_APLL_860,
	.mpll_mnod	= T20_MPLL_1000,
	.cpccr		= T20_CPCCR,
};

/* T20L / lite: APLL 712, 64 MB. */
const struct ingenic_t20_ddr_variant ingenic_t20_ddr_variant_t20l = {
	.name		= "T20L",
	.cpu_mhz	= 712,
	T20_DDR2_500_64M,
	.apll_mnod	= T20_APLL_712,
	.mpll_mnod	= T20_MPLL_1000,
	.cpccr		= T20_CPCCR,
};

/* T20X: APLL 860, 128 MB. */
const struct ingenic_t20_ddr_variant ingenic_t20_ddr_variant_t20x = {
	.name		= "T20X",
	.cpu_mhz	= 860,
	T20_DDR2_500_128M,
	.apll_mnod	= T20_APLL_860,
	.mpll_mnod	= T20_MPLL_1000,
	.cpccr		= T20_CPCCR,
};
