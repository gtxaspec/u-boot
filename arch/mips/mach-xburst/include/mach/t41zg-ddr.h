/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T41ZG LPDDR2 controller + Innophy PHY register values.
 *
 * Direct output of vendor U-Boot `tools/ingenic-tools/ddr_creator_chip/
 * ddr_params_creator` built from the T41-1.2.6 branch with
 * `isvp_t41zg_sfc_nor` defconfig (CONFIG_T41ZG +
 * CONFIG_LPDDR2_M54D2G16128ACR11 + DDR_500M + APLL_1104M + DW32=0,
 * CS0-only).
 *
 * 256 MiB single M54D2G16128ACR11 chip, 16-bit LPDDR2 @ 500 MHz,
 * APLL=1104, MPLL=1000, VPLL=1080.
 *
 * Untested in mainline (only T41NQ has HW-validated sdram_init right
 * now; the other variant DDR setpoints live here as documentation /
 * future-port reference until sdram.c gets a vendor-style ddr_params
 * refactor that consumes these values).
 */

#ifndef __T41ZG_DDR_H__
#define __T41ZG_DDR_H__

#define CONFIG_T41_LPDDR2		1

/* DDR controller geometry (from ddr_params_creator output) */
#define DDR_ROW			13
#define DDR_COL			10
#define DDR_BANK8		1
#define CONFIG_DDR_DW32		0	/* 16-bit bus */
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0
#define DDR_CHIP_0_SIZE		0x10000000	/* 256 MiB */
#define DDR_CHIP_1_SIZE		0

/* Clock target: MPLL=1000 -> DDR=MPLL/2=500 MHz */
#define DDR_MPLL_RATE		1000000000U
#define DDR_TARGET_RATE		(DDR_MPLL_RATE / 2U)

/* T41ZG PLL MNOD - vendor T41-1.2.6 isvp_t41.h. Bit layout:
 *   PLLEN[0], LOCK[2], PLL_ON[3], PLLRG[6:4],
 *   PLLOD1[10:7], PLLOD0[13:11], PLLN[19:14], PLLM[28:20].
 */
#define T41_APLL_MNOD		((0x5b  << 20) | (0 << 14) | (1 << 11) | (1 << 7) | (3 << 4))
#define T41_MPLL_MNOD		((0x7c  << 20) | (0 << 14) | (1 << 11) | (2 << 7) | (3 << 4))
#define T41_VPLL_MNOD		((0x59  << 20) | (0 << 14) | (1 << 11) | (1 << 7) | (3 << 4))

/* DDRC register values from ddr_params_creator (T41ZG + M54D2G16128ACR11 + 500 MHz + 16-bit) */
#define DDRC_CFG_VALUE		0x02004a2d
#define DDRC_CTRL_VALUE		0x0000b092
#define DDRC_DLMR_VALUE		0x00000000
#define DDRC_MMAP0_VALUE	0x000020f0
#define DDRC_MMAP1_VALUE	0x00003000
#define DDRC_REFCNT_VALUE	0x5cf20081
#define DDRC_TIMING1_VALUE	0x040d0804
#define DDRC_TIMING2_VALUE	0x060c0408
#define DDRC_TIMING3_VALUE	0x030a040a
#define DDRC_TIMING4_VALUE	0x191b2305
#define DDRC_TIMING5_VALUE	0x0f082043
#define DDRC_AUTOSR_CNT_VALUE	0x1b000f3c
#define DDRC_AUTOSR_EN_VALUE	0x00000000
#define DDRC_HREGPRO_VALUE	0x00000001
#define DDRC_PREGPRO_VALUE	0x00000001
#define DDRC_CGUC0_VALUE	0x11111111
#define DDRC_CGUC1_VALUE	0x00000113

/* PHY (Innophy LPDDR2) */
#define DDRP_MEMCFG_VALUE	0x00000009
#define DDRP_CL_VALUE		0x00000008
#define DDRP_CWL_VALUE		0x00000004
#define DDR_MR0_VALUE		0x00000000
#define DDR_MR1_VALUE		0x000001c3
#define DDR_MR2_VALUE		0x00000206
#define DDR_MR3_VALUE		0x00000300

/* REMMAP_ARRAY from ddr_params_creator */
#define T41_REMMAP_ARRAY	{ 0x030f0e0d, 0x07060504, 0x0b0a0908, \
			  0x0201000c, 0x13121110 }

#endif /* __T41ZG_DDR_H__ */
