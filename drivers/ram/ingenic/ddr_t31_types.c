// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 (XBurst1) DDR - per-SKU register/clock tables.
 *
 * One const struct per T31 variant, selected at probe time from the DT
 * node compatible (ddr_t31.c of_match .data). These replace the former
 * compile-time CONFIG_T31_VARIANT_* / #if branches in
 * arch/mips/mach-xburst/include/mach/t31-ddr.h - the numeric values are
 * the same verbatim vendor ddr_params_creator GOLD output.
 *
 * Clocks: DDR = MPLL/2. 64M DDR2 (N/L/LC) run MPLL 1008 / DDR 504; 128M
 * DDR2 (X/AL) and C100 (DDR3) run MPLL 1200 / DDR 600; T31A (DDR3) runs
 * MPLL 1512 / DDR 756. APLL (CPU) is per-SKU.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <linux/types.h>
#include "ddr_t31.h"

/*
 * CPAPCR/CPMPCR encoding (N=1, OD0=1, OD1=2): Pllout = 12 MHz * M, so
 * M = MHz / 12. Same scheme the former t31/pll.c used.
 */
#define T31_PLL_MNOD(m)		(((m) << 20) | (1 << 14) | (2 << 11) | (1 << 8))

/*
 * CPCCR divider words: SCLKA=APLL, CPU<-APLL, H0/H2<-MPLL, DIV_L2=2,
 * DIV_CPU=1. The H0/H2/PCLK band tracks the MPLL rate, exactly as the
 * former t31/pll.c selected (#if CONFIG_T31_MPLL_MHZ <= 1008).
 */
#define T31_CPCCR_MPLL1008	0x9a073310u	/* PCLK 8 / H2 4 / H0 4 */
#define T31_CPCCR_MPLL1200	0x9a0b5510u	/* PCLK 12 / H2 6 / H0 6 */

#define T31_DDRC_CTRL_VALUE	0x0000d91e

/* ---- DDR2 64 MB, M14D5121632A, 4-bank, MPLL 1008 / DDR 504 ---- */
#define T31_DDR2_64M_FIELDS \
	.type		= T31_DDR_TYPE_DDR2, \
	.chip		= "M14D5121632A", \
	.row		= 13, \
	.col		= 10, \
	.bank8		= 0, \
	.cs0		= 1, \
	.cs1		= 0, \
	.dw32		= 0, \
	.chip0_size	= 0x04000000, \
	.mpll_mnod	= T31_PLL_MNOD(84), \
	.cpccr		= T31_CPCCR_MPLL1008, \
	.ddrc_cfg	= 0x0a288a40, \
	.ddrc_ctrl	= T31_DDRC_CTRL_VALUE, \
	.ddrc_mmap0	= 0x000020fc, \
	.ddrc_mmap1	= 0x00002400, \
	.ddrc_refcnt	= 0x00f20001, \
	.ddrc_timing	= { 0x040e0806, 0x02170707, 0x2007051e, 0x1a240031, \
			    0xff060405, 0x32170505 }, \
	.mr0		= 0x00000f73

/* ---- DDR2 128 MB, M14D1G1664A, 8-bank, MPLL 1200 / DDR 600 ---- */
#define T31_DDR2_128M_FIELDS \
	.type		= T31_DDR_TYPE_DDR2, \
	.chip		= "M14D1G1664A", \
	.row		= 13, \
	.col		= 10, \
	.bank8		= 1, \
	.cs0		= 1, \
	.cs1		= 0, \
	.dw32		= 0, \
	.chip0_size	= 0x08000000, \
	.mpll_mnod	= T31_PLL_MNOD(100), \
	.cpccr		= T31_CPCCR_MPLL1200, \
	.ddrc_cfg	= 0x0aa88a42, \
	.ddrc_ctrl	= T31_DDRC_CTRL_VALUE, \
	.ddrc_mmap0	= 0x000020f8, \
	.ddrc_mmap1	= 0x00002800, \
	.ddrc_refcnt	= 0x00910003, \
	.ddrc_timing	= { 0x050f0a06, 0x021c0a07, 0x200a0722, 0x26240031, \
			    0xff060405, 0x321c0505 }, \
	.mr0		= 0x00000f73

/* ----------------------- DDR2-64M SKUs ----------------------- */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31n = {
	.name		= "T31N",
	.cpu_mhz	= 1404,
	.apll_mnod	= T31_PLL_MNOD(117),	/* 1404 */
	T31_DDR2_64M_FIELDS,
};

const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31l = {
	.name		= "T31L",
	.cpu_mhz	= 1008,
	.apll_mnod	= T31_PLL_MNOD(84),	/* 1008 */
	T31_DDR2_64M_FIELDS,
};

const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31lc = {
	.name		= "T31LC",
	.cpu_mhz	= 1104,
	.apll_mnod	= T31_PLL_MNOD(92),	/* 1104 */
	T31_DDR2_64M_FIELDS,
};

/* ----------------------- DDR2-128M SKUs ----------------------- */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31x = {
	.name		= "T31X",
	.cpu_mhz	= 1392,
	.apll_mnod	= T31_PLL_MNOD(116),	/* 1392 */
	T31_DDR2_128M_FIELDS,
};

const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31al = {
	.name		= "T31AL",
	.cpu_mhz	= 1392,
	.apll_mnod	= T31_PLL_MNOD(116),	/* 1392 - same silicon/clocks as T31X */
	T31_DDR2_128M_FIELDS,
};

/* ----------------------- DDR3-128M SKUs ----------------------- */
const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_t31a = {
	.name		= "T31A",
	.chip		= "M15T1G1664A",
	.type		= T31_DDR_TYPE_DDR3,
	.cpu_mhz	= 1512,
	.row		= 13,
	.col		= 10,
	.bank8		= 1,
	.cs0		= 1,
	.cs1		= 0,
	.dw32		= 0,
	.chip0_size	= 0x08000000,
	.apll_mnod	= T31_PLL_MNOD(126),	/* 1512 */
	.mpll_mnod	= T31_PLL_MNOD(126),	/* 1512 -> DDR 756 */
	.cpccr		= T31_CPCCR_MPLL1200,	/* MPLL > 1008 -> PCLK12/H2 6/H0 6 */
	.ddrc_cfg	= 0x0aac8a42,
	.ddrc_ctrl	= T31_DDRC_CTRL_VALUE,
	.ddrc_mmap0	= 0x000020f8,
	.ddrc_mmap1	= 0x00002800,
	.ddrc_refcnt	= 0x00b60003,
	.ddrc_timing	= { 0x06100c06, 0x041d0b08, 0x210b0627, 0x3c250043,
			    0xff080505, 0x80220505 },
	.mr0		= 0x00001c40,
};

const struct ingenic_t31_ddr_variant ingenic_t31_ddr_variant_c100 = {
	.name		= "C100",
	.chip		= "M15T1G1664A",
	.type		= T31_DDR_TYPE_DDR3,
	.cpu_mhz	= 1392,
	.row		= 13,
	.col		= 10,
	.bank8		= 1,
	.cs0		= 1,
	.cs1		= 0,
	.dw32		= 0,
	.chip0_size	= 0x08000000,
	.apll_mnod	= T31_PLL_MNOD(116),	/* 1392 */
	.mpll_mnod	= T31_PLL_MNOD(100),	/* 1200 -> DDR 600 */
	.cpccr		= T31_CPCCR_MPLL1200,
	.ddrc_cfg	= 0x0aac8a42,
	.ddrc_ctrl	= T31_DDRC_CTRL_VALUE,
	.ddrc_mmap0	= 0x000020f8,
	.ddrc_mmap1	= 0x00002800,
	.ddrc_refcnt	= 0x00910003,
	.ddrc_timing	= { 0x050f0a06, 0x04170908, 0x2109051f, 0x30250043,
			    0xff080505, 0x801c0505 },
	.mr0		= 0x00001a40,
};
