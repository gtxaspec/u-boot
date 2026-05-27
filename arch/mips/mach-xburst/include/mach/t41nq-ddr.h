/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T41NQ DDR3 controller + Innophy PHY register values.
 *
 * Stage 1 placeholder - real DDR3 16-bit values come from running
 * vendor `tools/ingenic-tools/ddr_params_creator` with:
 *   CONFIG_T41NQ + CONFIG_DDR3_W631GU6NG + DDR=700MHz + DW32=0
 *   + CS0-only
 * and copying the generated register set in Stage 2.
 *
 * The T41NQ DDR3 differs from T40XP DDR3 in:
 *   - 16-bit bus (DW32=0) vs T40XP's 32-bit
 *   - Different DDR chip (W631GU6NG vs T40XP M15T1G1664A_2C)
 *   - DDR clock 700 MHz vs T40XP's 600 MHz
 *
 * Per vendor isvp_t41.h T41NQ block:
 *   - 128 MiB single chip W631GU6NG (16-bit DDR3-1400)
 *   - APLL = 1104 MHz (M=92, N=1, OD1=2, OD0=1)
 *   - MPLL = 1400 MHz (M=350, N=2, OD1=3, OD0=1)
 */

#ifndef __T41NQ_DDR_H__
#define __T41NQ_DDR_H__

#define CONFIG_T41_DDR3		1	/* DDR3 path in sdram.c */

/* DDR controller geometry - placeholder, refine from params_creator */
#define DDR_ROW			13
#define DDR_COL			10
#define DDR_BANK8		1	/* 8-bank DDR3 */
#define CONFIG_DDR_DW32		0	/* 16-bit bus (T41NQ) */
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0
#define DDR_CHIP_0_SIZE		0x08000000	/* 128 MiB */
#define DDR_CHIP_1_SIZE		0

/* Clock target: MPLL=1400 -> DDR=MPLL/2=700 MHz */
#define DDR_MPLL_RATE		1400000000U
#define DDR_TARGET_RATE		(DDR_MPLL_RATE / 2U)

/* T41NQ per vendor isvp_t41.h:
 *   APLL = 1104 MHz: M=92, N=1, OD1=2, OD0=1, (92/1)*24/(2*1) = 1104
 *   MPLL = 1400 MHz: M=350, N=2, OD1=3, OD0=1, (350/2)*24/(3*1) = 1400
 *   VPLL = 1188 MHz: M=99, N=1, OD1=2, OD0=1, (99/1)*24/(2*1) = 1188
 */
#define T41_APLL_MNOD		((92 << 20) | (1 << 14) | (2 << 11) | (1 << 8))
#define T41_MPLL_MNOD		((350 << 20) | (2 << 14) | (3 << 11) | (1 << 8))
#define T41_VPLL_MNOD		((99 << 20) | (1 << 14) | (2 << 11) | (1 << 8))

/*
 * DDR controller and PHY register values - STAGE 1 PLACEHOLDERS
 * (copied from T40XP - DO NOT trust these on T41NQ silicon, will be
 * replaced from ddr_params_creator output in Stage 2).
 */
#define DDRC_CFG_VALUE		0x2a002a35
#define DDRC_CTRL_VALUE		0x0000b092
#define DDRC_DLMR_VALUE		0x00000002
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

#define DDRP_MEMCFG_VALUE	0x00000010
#define DDRP_CL_VALUE		0x0000000b
#define DDRP_CWL_VALUE		0x00000009
#define DDR_MR0_VALUE		0x00001b70
#define DDR_MR1_VALUE		0x00010042
#define DDR_MR2_VALUE		0x00020018
#define DDR_MR3_VALUE		0x00030000

#define T41_REMMAP_ARRAY	{ 0x030f0e0d, 0x07060504, 0x0b0a0908, \
				  0x0201000c, 0x13121110 }

#endif /* __T41NQ_DDR_H__ */
