/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T40A DDR3 controller + Innophy PHY register values.
 *
 * Direct output of vendor U-Boot `tools/ingenic-tools/
 * ddr_params_creator` built from the T40-1.3.1 branch with
 * `isvp_t40a_sfcnor` (CONFIG_T40A + DDR_700M + APLL_1008M +
 * CONFIG_DDR3_W634GU6Q8 + CONFIG_DDR_DW32=1, CS0-only).
 *
 * Note T40-1.3.1 picked W634GU6Q8 for T40A (an older T40-main branch
 * targeted M15T4G16256A_2S instead). Both are 4 Gbit DDR3 dies; the
 * timings here are for W634GU6Q8 at MPLL=1400/DDR=700 MHz.
 *
 * Dual-rank ~1 GiB physical (ddr_params_creator output); vendor
 * isvp_common.h tags T40A as CONFIG_DDR_128M for SoC-side addressing.
 * Mainline does not have a T40A board to confirm which value the bus
 * actually exposes.
 *
 * Untested in mainline (no T40A hardware in the lab).
 */

#ifndef __T40A_DDR_H__
#define __T40A_DDR_H__

#define CONFIG_T40_DDR3		1	/* used by sdram.c for DDR3 path */

/* DDR controller geometry */
#define DDR_ROW			16
#define DDR_COL			10
#define DDR_BANK8		1	/* 8-bank DDR3 */
#define CONFIG_DDR_DW32		1
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0
#define DDR_CHIP_0_SIZE		0x40000000	/* 1 GiB dual-rank W634GU6Q8 per ddr_params_creator */
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
#define DDRC_DLMR_VALUE		0x00000004
#define DDRC_MMAP0_VALUE	0x00000080
#define DDRC_MMAP1_VALUE	0x0000ff00
#define DDRC_REFCNT_VALUE	0x5caa0083
#define DDRC_TIMING1_VALUE	0x05110c06
#define DDRC_TIMING2_VALUE	0x04060607
#define DDRC_TIMING3_VALUE	0x030b050b
#define DDRC_TIMING4_VALUE	0x1a241a05
#define DDRC_TIMING5_VALUE	0x80048054
#define DDRC_AUTOSR_CNT_VALUE	0x5b001556
#define DDRC_AUTOSR_EN_VALUE	0x00000000
#define DDRC_HREGPRO_VALUE	0x00000001
#define DDRC_PREGPRO_VALUE	0x00000001
#define DDRC_CGUC0_VALUE	0x11111111
#define DDRC_CGUC1_VALUE	0x00000113

/* PHY (Innophy DDR3) */
#define DDRP_MEMCFG_VALUE	0x00000010
#define DDRP_CL_VALUE		0x00000007
#define DDRP_CWL_VALUE		0x00000005
#define DDR_MR0_VALUE		0x00001d30
#define DDR_MR1_VALUE		0x00010040
#define DDR_MR2_VALUE		0x00020008
#define DDR_MR3_VALUE		0x00030000

/* REMMAP_ARRAY from ddr_params_creator */
#define T40_REMMAP_ARRAY	{ 0x0311100f, 0x07060504, 0x0b0a0908, \
				  0x000e0d0c, 0x13120201 }

#endif /* __T40A_DDR_H__ */
