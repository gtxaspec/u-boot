/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T40N DDR2 controller + Innophy PHY register values.
 *
 * Output of vendor `tools/ingenic-tools/ddr_params_creator` host
 * tool with CONFIG_T40N + M14D5121632A_DDR2 + DDR=500MHz + DW32=1
 * + CS0-only inputs. Matches the values vendor T40 U-Boot 2013
 * production builds generate at build time.
 *
 * 128 MiB single chip, 32-bit DDR2 @ 500 MHz, MPLL=1000, APLL=912.
 */

#ifndef __T40N_DDR_H__
#define __T40N_DDR_H__

/* DDR controller geometry */
#define DDR_ROW			13
#define DDR_COL			10
#define DDR_BANK8		0	/* 4-bank DDR2 */
#define CONFIG_DDR_DW32		1
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0
#define DDR_CHIP_0_SIZE		0x08000000	/* 128 MiB */
#define DDR_CHIP_1_SIZE		0

/* Clock target: MPLL=1000 -> DDR=MPLL/2=500 MHz */
#define DDR_MPLL_RATE		1000000000U
#define DDR_TARGET_RATE		(DDR_MPLL_RATE / 2U)

/* APLL and MPLL M/N/OD encoding (M<<20 | N<<14 | OD1<<11 | OD0<<8) */
#define T40_APLL_MNOD		((76 << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define T40_MPLL_MNOD		((125 << 20) | (1 << 14) | (3 << 11) | (1 << 8))

/* DDRC register values from ddr_params_creator */
#define DDRC_CFG_VALUE		0x28002825
#define DDRC_CTRL_VALUE		0x00008092
#define DDRC_MMAP0_VALUE	0x000020f8
#define DDRC_MMAP1_VALUE	0x00002800
#define DDRC_REFCNT_VALUE	0x21f20081
#define DDRC_TIMING1_VALUE	0x050c0806
#define DDRC_TIMING2_VALUE	0x04060407
#define DDRC_TIMING3_VALUE	0x03080208
#define DDRC_TIMING4_VALUE	0x1e1e1705
#define DDRC_TIMING5_VALUE	0x46000055
#define DDRC_AUTOSR_CNT_VALUE	0x20000f3c
#define DDRC_AUTOSR_EN_VALUE	0x00000000
#define DDRC_HREGPRO_VALUE	0x00000001
#define DDRC_PREGPRO_VALUE	0x00000001
#define DDRC_CGUC0_VALUE	0x11111111
#define DDRC_CGUC1_VALUE	0x00000113

/* PHY (Innophy DDR2) */
#define DDRP_MEMCFG_VALUE	0x00000011	/* DDR2, burst 8 */
#define DDRP_CL_VALUE		0x00000007
#define DDRP_CWL_VALUE		0x00000005
#define DDR_MR0_VALUE		0x00000f73
#define DDR_MR1_VALUE		0x00002040
#define DDR_MR2_VALUE		0x00004000
#define DDR_MR3_VALUE		0x00006000

/* REMMAP_ARRAY from ddr_params_creator */
#define T40_REMMAP_ARRAY	{ 0x03020e0d, 0x07060504, 0x0b0a0908, \
				  0x0f01000c, 0x13121110 }

#endif /* __T40N_DDR_H__ */
