/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T40XP DDR3 controller + Innophy PHY register values.
 *
 * Output of vendor `tools/ingenic-tools/ddr_params_creator` host
 * tool with CONFIG_T40XP + M15T1G1664A_2C + DDR=600MHz + DW32=1
 * + CS0-only inputs.
 *
 * 256 MiB single chip, 32-bit DDR3-1333 @ 600 MHz, MPLL=1200,
 * APLL=1008.
 *
 * Untested on real silicon yet - T40XP board lab connector
 * power=B0R11, boot=B1R10.
 */

#ifndef __T40XP_DDR_H__
#define __T40XP_DDR_H__

#define CONFIG_T40_DDR3		1	/* used by sdram.c for DDR3 path */

/* DDR controller geometry */
#define DDR_ROW			13
#define DDR_COL			10
#define DDR_BANK8		1	/* 8-bank DDR3 */
#define CONFIG_DDR_DW32		1
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0
#define DDR_CHIP_0_SIZE		0x10000000	/* 256 MiB */
#define DDR_CHIP_1_SIZE		0

/* Clock target: MPLL=1200 -> DDR=MPLL/2=600 MHz */
#define DDR_MPLL_RATE		1200000000U
#define DDR_TARGET_RATE		(DDR_MPLL_RATE / 2U)

/* T40XP per vendor isvp_t40.h: APLL=1008 MHz (M=84), MPLL=1200 MHz */
#define T40_APLL_MNOD		((84 << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define T40_MPLL_MNOD		((100 << 20) | (1 << 14) | (2 << 11) | (1 << 8))

/* DDRC register values from ddr_params_creator */
#define DDRC_CFG_VALUE		0x2a002a35
#define DDRC_CTRL_VALUE		0x0000b092
#define DDRC_DLMR_VALUE		0x00000002	/* DDR3-specific */
#define DDRC_MMAP0_VALUE	0x000020f0
#define DDRC_MMAP1_VALUE	0x00003000
#define DDRC_REFCNT_VALUE	0x62910083
#define DDRC_TIMING1_VALUE	0x07110a08
#define DDRC_TIMING2_VALUE	0x0809050b
#define DDRC_TIMING3_VALUE	0x03090409
#define DDRC_TIMING4_VALUE	0x1c201705
#define DDRC_TIMING5_VALUE	0x80057054
#define DDRC_AUTOSR_CNT_VALUE	0x21001249
#define DDRC_AUTOSR_EN_VALUE	0x00000000
#define DDRC_HREGPRO_VALUE	0x00000001
#define DDRC_PREGPRO_VALUE	0x00000001
#define DDRC_CGUC0_VALUE	0x11111111
#define DDRC_CGUC1_VALUE	0x00000113

/* PHY (Innophy DDR3) */
#define DDRP_MEMCFG_VALUE	0x00000010	/* DDR3, burst 8 */
#define DDRP_CL_VALUE		0x0000000b	/* CL=11 */
#define DDRP_CWL_VALUE		0x00000009	/* CWL=9 */
#define DDR_MR0_VALUE		0x00001b70
#define DDR_MR1_VALUE		0x00010042
#define DDR_MR2_VALUE		0x00020018
#define DDR_MR3_VALUE		0x00030000

/* REMMAP_ARRAY from ddr_params_creator */
#define T40_REMMAP_ARRAY	{ 0x030f0e0d, 0x07060504, 0x0b0a0908, \
				  0x0201000c, 0x13121110 }

#endif /* __T40XP_DDR_H__ */
