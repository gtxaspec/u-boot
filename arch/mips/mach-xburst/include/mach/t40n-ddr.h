/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T40N DDR2 controller + Innophy PHY register values.
 *
 * Vendor T40N setpoint per isvp_common.h: 128 MiB single chip, 32-bit
 * DDR2 @ 550 MHz, MPLL=1100, APLL=912. M14D5121632A chip.
 *
 * Values are direct output of vendor `tools/ingenic-tools/
 * ddr_params_creator` host-tool configured with `isvp_t40n_sfcnor`
 * (CONFIG_T40N + CONFIG_DDR2_M14D5121632A + DDR_550M + DW32=1, CS0
 * only). Untested in mainline - selecting T40_VARIANT_T40N runs the
 * Innophy DDR2 init from these values at the vendor 550 MHz target.
 * For the cloner-derived 500 MHz timings that the lab T40 board
 * ("T40NN") boots cleanly on, use T40_VARIANT_T40NN (t40nn-ddr.h).
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

/* Clock target: MPLL=1100 -> DDR=MPLL/2=550 MHz */
#define DDR_MPLL_RATE		1100000000U
#define DDR_TARGET_RATE		(DDR_MPLL_RATE / 2U)

/* APLL=912 MHz, MPLL=1100 MHz (M<<20 | N<<14 | OD1<<11 | OD0<<8) */
#define T40_APLL_MNOD		((76 << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define T40_MPLL_MNOD		((275 << 20) | (2 << 14) | (3 << 11) | (1 << 8))

/* DDRC register values from ddr_params_creator */
#define DDRC_CFG_VALUE		0x28002825
#define DDRC_CTRL_VALUE		0x00008092
#define DDRC_MMAP0_VALUE	0x000020f8
#define DDRC_MMAP1_VALUE	0x00002800
#define DDRC_REFCNT_VALUE	0x24850083
#define DDRC_TIMING1_VALUE	0x050d0906
#define DDRC_TIMING2_VALUE	0x04060507
#define DDRC_TIMING3_VALUE	0x03090209
#define DDRC_TIMING4_VALUE	0x22201906
#define DDRC_TIMING5_VALUE	0x4e000055
#define DDRC_AUTOSR_CNT_VALUE	0x230010c2
#define DDRC_AUTOSR_EN_VALUE	0x00000000
#define DDRC_HREGPRO_VALUE	0x00000001
#define DDRC_PREGPRO_VALUE	0x00000001
#define DDRC_CGUC0_VALUE	0x11111111
#define DDRC_CGUC1_VALUE	0x00000113

/* PHY (Innophy DDR2) */
#define DDRP_MEMCFG_VALUE	0x00000011	/* DDR2, burst 8 */
#define DDRP_CL_VALUE		0x00000007
#define DDRP_CWL_VALUE		0x00000005
#define DDR_MR0_VALUE		0x00000173
#define DDR_MR1_VALUE		0x00002040
#define DDR_MR2_VALUE		0x00004000
#define DDR_MR3_VALUE		0x00006000

/* REMMAP_ARRAY from ddr_params_creator */
#define T40_REMMAP_ARRAY	{ 0x03020e0d, 0x07060504, 0x0b0a0908, \
				  0x0f01000c, 0x13121110 }

#endif /* __T40N_DDR_H__ */
