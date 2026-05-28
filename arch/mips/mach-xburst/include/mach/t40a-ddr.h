/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T40A DDR3 controller + Innophy PHY register values.
 *
 * Vendor T40A setpoint per isvp_common.h: dual-rank M15T4G16256A_2S
 * DDR3 (4 Gbit/rank x 2 = 1 GiB total), 32-bit, DDR=700 MHz,
 * MPLL=1400, APLL=1008. Vendor isvp_common.h tags T40A as
 * CONFIG_DDR_128M for SoC-side addressing but ddr_params_creator
 * sizes the chip at 1 GiB; mainline does not have a T40A board to
 * confirm which value the bus actually exposes.
 *
 * Values are direct output of vendor `tools/ingenic-tools/
 * ddr_params_creator` host-tool configured with `isvp_t40a_sfcnor`.
 * Untested in mainline (no T40A hardware in the lab) - selecting
 * T40_VARIANT_T40A runs the Innophy DDR3 init from these values at
 * the vendor 700 MHz target.
 */

#ifndef __T40A_DDR_H__
#define __T40A_DDR_H__

/* DDR controller geometry */
#define DDR_ROW			16
#define DDR_COL			10
#define DDR_BANK8		1	/* 8-bank DDR3 */
#define CONFIG_DDR_DW32		1
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0
#define DDR_CHIP_0_SIZE		0x40000000	/* 1 GiB per ddr_params_creator (dual-rank M15T4G16256A_2S) */
#define DDR_CHIP_1_SIZE		0

/* Clock target: MPLL=1400 -> DDR=MPLL/2=700 MHz */
#define DDR_MPLL_RATE		1400000000U
#define DDR_TARGET_RATE		(DDR_MPLL_RATE / 2U)

/* APLL=1008 MHz, MPLL=1400 MHz (M<<20 | N<<14 | OD1<<11 | OD0<<8) */
#define T40_APLL_MNOD		((84  << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define T40_MPLL_MNOD		((350 << 20) | (2 << 14) | (3 << 11) | (1 << 8))

/* DDRC register values from ddr_params_creator */
#define DDRC_CFG_VALUE		0x6a006a35
#define DDRC_CTRL_VALUE		0x0000b092
#define DDRC_MMAP0_VALUE	0x00000080
#define DDRC_MMAP1_VALUE	0x0000ff00
#define DDRC_REFCNT_VALUE	0x79aa0083
#define DDRC_TIMING1_VALUE	0x08130b09
#define DDRC_TIMING2_VALUE	0x0705060a
#define DDRC_TIMING3_VALUE	0x030a040a
#define DDRC_TIMING4_VALUE	0x20241b06
#define DDRC_TIMING5_VALUE	0x80048044
#define DDRC_AUTOSR_CNT_VALUE	0x38001556
#define DDRC_AUTOSR_EN_VALUE	0x00000000
#define DDRC_HREGPRO_VALUE	0x00000001
#define DDRC_PREGPRO_VALUE	0x00000001
#define DDRC_CGUC0_VALUE	0x11111111
#define DDRC_CGUC1_VALUE	0x00000113

/* PHY (Innophy DDR3) */
#define DDRP_MEMCFG_VALUE	0x00000010
#define DDRP_CL_VALUE		0x0000000a
#define DDRP_CWL_VALUE		0x00000008
#define DDR_MR0_VALUE		0x00001d60
#define DDR_MR1_VALUE		0x00010042
#define DDR_MR2_VALUE		0x00020020
#define DDR_MR3_VALUE		0x00030000

/* REMMAP_ARRAY from ddr_params_creator */
#define T40_REMMAP_ARRAY	{ 0x0311100f, 0x07060504, 0x0b0a0908, \
				  0x000e0d0c, 0x13120201 }

#endif /* __T40A_DDR_H__ */
