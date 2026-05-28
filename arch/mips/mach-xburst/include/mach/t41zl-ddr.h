/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T41ZL DDR2 controller + Innophy PHY register values.
 *
 * Direct output of vendor U-Boot `tools/ingenic-tools/ddr_creator_chip/
 * ddr_params_creator` built from the T41-1.2.6 branch with
 * `isvp_t41zl_sfc_nor` defconfig (CONFIG_T41ZL +
 * CONFIG_DDR2_M14D5121632A + DDR_600M + APLL_1104M + DW32=0,
 * CS0-only).
 *
 * 64 MiB single M14D5121632A chip, 16-bit DDR2 @ 600 MHz,
 * APLL=1104, MPLL=1200, VPLL=1080.
 *
 * Untested in mainline (only T41NQ has HW-validated sdram_init right
 * now; the other variant DDR setpoints live here as documentation /
 * future-port reference until sdram.c gets a vendor-style ddr_params
 * refactor that consumes these values).
 */

#ifndef __T41ZL_DDR_H__
#define __T41ZL_DDR_H__

#define CONFIG_T41_DDR2		1

/* DDR controller geometry (from ddr_params_creator output) */
#define DDR_ROW			13
#define DDR_COL			10
#define DDR_BANK8		0
#define CONFIG_DDR_DW32		0	/* 16-bit bus */
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0
#define DDR_CHIP_0_SIZE		0x04000000	/* 64 MiB */
#define DDR_CHIP_1_SIZE		0

/* Clock target: MPLL=1200 -> DDR=MPLL/2=600 MHz */
#define DDR_MPLL_RATE		1200000000U
#define DDR_TARGET_RATE		(DDR_MPLL_RATE / 2U)

/* T41ZL PLL MNOD - vendor T41-1.2.6 isvp_t41.h. Bit layout:
 *   PLLEN[0], LOCK[2], PLL_ON[3], PLLRG[6:4],
 *   PLLOD1[10:7], PLLOD0[13:11], PLLN[19:14], PLLM[28:20].
 */
#define T41_APLL_MNOD		((0x5b  << 20) | (0 << 14) | (1 << 11) | (1 << 7) | (3 << 4))
#define T41_MPLL_MNOD		((0x63  << 20) | (0 << 14) | (1 << 11) | (1 << 7) | (3 << 4))
#define T41_VPLL_MNOD		((0x59  << 20) | (0 << 14) | (1 << 11) | (1 << 7) | (3 << 4))

/* DDRC register values from ddr_params_creator (T41ZL + M14D5121632A + 600 MHz + 16-bit) */
#define DDRC_CFG_VALUE		0x00002825
#define DDRC_CTRL_VALUE		0x0000b092
#define DDRC_DLMR_VALUE		0x00000000
#define DDRC_MMAP0_VALUE	0x000020fc
#define DDRC_MMAP1_VALUE	0x00002400
#define DDRC_REFCNT_VALUE	0x68910083
#define DDRC_TIMING1_VALUE	0x050d0a06
#define DDRC_TIMING2_VALUE	0x04060507
#define DDRC_TIMING3_VALUE	0x030a020a
#define DDRC_TIMING4_VALUE	0x25231c07
#define DDRC_TIMING5_VALUE	0x55000055
#define DDRC_AUTOSR_CNT_VALUE	0x27001249
#define DDRC_AUTOSR_EN_VALUE	0x00000000
#define DDRC_HREGPRO_VALUE	0x00000001
#define DDRC_PREGPRO_VALUE	0x00000001
#define DDRC_CGUC0_VALUE	0x11111111
#define DDRC_CGUC1_VALUE	0x00000113

/* PHY (Innophy DDR2) */
#define DDRP_MEMCFG_VALUE	0x00000008
#define DDRP_CL_VALUE		0x00000007
#define DDRP_CWL_VALUE		0x00000005
#define DDR_MR0_VALUE		0x00000173
#define DDR_MR1_VALUE		0x00002040
#define DDR_MR2_VALUE		0x00004000
#define DDR_MR3_VALUE		0x00006000

/* REMMAP_ARRAY from ddr_params_creator */
#define T41_REMMAP_ARRAY	{ 0x03020d0c, 0x07060504, 0x0b0a0908, \
			  0x0f0e0100, 0x13121110 }

#endif /* __T41ZL_DDR_H__ */
