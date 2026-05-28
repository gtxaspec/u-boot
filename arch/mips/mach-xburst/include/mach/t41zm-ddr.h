/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T41ZM LPDDR3 controller + Innophy PHY register values.
 *
 * Direct output of vendor U-Boot `tools/ingenic-tools/ddr_creator_chip/
 * ddr_params_creator` built from the T41-1.2.6 branch with
 * `isvp_t41zm_sfc_nor` defconfig (CONFIG_T41ZM +
 * CONFIG_LPDDR3_W63AH6N2BBJ + DDR_700M + APLL_1104M + DW32=0,
 * CS0-only).
 *
 * 128 MiB single W63AH6N2B_BJ chip, 16-bit LPDDR3 @ 700 MHz,
 * APLL=1104, MPLL=1400, VPLL=1080.
 *
 * Untested in mainline (only T41NQ has HW-validated sdram_init right
 * now; the other variant DDR setpoints live here as documentation /
 * future-port reference until sdram.c gets a vendor-style ddr_params
 * refactor that consumes these values).
 */

#ifndef __T41ZM_DDR_H__
#define __T41ZM_DDR_H__

#define CONFIG_T41_LPDDR3		1

/* DDR controller geometry (from ddr_params_creator output) */
#define DDR_ROW			13
#define DDR_COL			10
#define DDR_BANK8		1
#define CONFIG_DDR_DW32		0	/* 16-bit bus */
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0
#define DDR_CHIP_0_SIZE		0x08000000	/* 128 MiB */
#define DDR_CHIP_1_SIZE		0

/* Clock target: MPLL=1400 -> DDR=MPLL/2=700 MHz */
#define DDR_MPLL_RATE		1400000000U
#define DDR_TARGET_RATE		(DDR_MPLL_RATE / 2U)

/* T41ZM PLL MNOD - vendor T41-1.2.6 isvp_t41.h. Bit layout:
 *   PLLEN[0], LOCK[2], PLL_ON[3], PLLRG[6:4],
 *   PLLOD1[10:7], PLLOD0[13:11], PLLN[19:14], PLLM[28:20].
 */
#define T41_APLL_MNOD		((0x5b  << 20) | (0 << 14) | (1 << 11) | (1 << 7) | (3 << 4))
#define T41_MPLL_MNOD		((0x15d << 20) | (2 << 14) | (1 << 11) | (1 << 7) | (1 << 4))
#define T41_VPLL_MNOD		((0x59  << 20) | (0 << 14) | (1 << 11) | (1 << 7) | (3 << 4))

/* DDRC register values from ddr_params_creator (T41ZM + W63AH6N2B_BJ + 700 MHz + 16-bit) */
#define DDRC_CFG_VALUE		0x02002a2d
#define DDRC_CTRL_VALUE		0x0000b092
#define DDRC_DLMR_VALUE		0x00000006
#define DDRC_MMAP0_VALUE	0x000020f8
#define DDRC_MMAP1_VALUE	0x00002800
#define DDRC_REFCNT_VALUE	0x6eaa0083
#define DDRC_TIMING1_VALUE	0x06110b06
#define DDRC_TIMING2_VALUE	0x0a0f060c
#define DDRC_TIMING3_VALUE	0x030d040f
#define DDRC_TIMING4_VALUE	0x242d1e08
#define DDRC_TIMING5_VALUE	0x190b0066
#define DDRC_AUTOSR_CNT_VALUE	0x2d001556
#define DDRC_AUTOSR_EN_VALUE	0x00000000
#define DDRC_HREGPRO_VALUE	0x00000001
#define DDRC_PREGPRO_VALUE	0x00000001
#define DDRC_CGUC0_VALUE	0x11111111
#define DDRC_CGUC1_VALUE	0x00000113

/* PHY (Innophy LPDDR3) */
#define DDRP_MEMCFG_VALUE	0x0000000b
#define DDRP_CL_VALUE		0x0000000c
#define DDRP_CWL_VALUE		0x00000006
#define DDR_MR0_VALUE		0x00000000
#define DDR_MR1_VALUE		0x00000123
#define DDR_MR2_VALUE		0x0000021a
#define DDR_MR3_VALUE		0x00000302

/* REMMAP_ARRAY from ddr_params_creator */
#define T41_REMMAP_ARRAY	{ 0x030e0d0c, 0x07060504, 0x0b0a0908, \
			  0x0f020100, 0x13121110 }

#endif /* __T41ZM_DDR_H__ */
