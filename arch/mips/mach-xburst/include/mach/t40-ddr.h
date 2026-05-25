/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T40 DDR2 controller and Innophy PHY register map
 *
 * Profile: T40, 16-bit, CS0 only. DDR clock = MPLL/2 = 500 MHz
 * (CONFIG_T40_MPLL_MHZ=1000). Default chip M14D5121632A 64 MB
 * 4-bank DDR2 - the same part the T31N/L/LC use at the same
 * ~500 MHz clock.
 *
 * T40 uses the legacy Ingenic DDRC (DDRC_BASE at 0xb34f0000)
 * paired with the Innophy DDR2 training PHY at 0xb3011000, so
 * the controller / PHY register map is structurally identical
 * to T31's (which also uses the same IP block); the DDRC_CFG /
 * REFCNT / TIMING / MR0 values come from the vendor T40
 * ddr_dwc.c / ddr_innophy.c.
 *
 * Untested on real T40 silicon - this is a structural port using
 * the T31 DDR2-@500 register set as the initial parameter cut.
 * Validation pass on real T40 hardware (lab unit 10.25.1.158) is
 * the next step; deltas surfaced there land in this header.
 */

#ifndef __T40_DDR_H__
#define __T40_DDR_H__

#include <mach/t40.h>		/* DDRC_BASE / DDR_PHY_BASE / CPM_BASE */

#define DDR_APB_BASE		0xb3012000

/*
 * DDR controller register offsets. T40 uses the Ingenic "Innophy"
 * DDRC register map (NOT the Synopsys DWC map T31 uses); offsets
 * follow vendor `arch/mips/include/asm/ddr_innophy.h`.
 *
 *           T31 DWC   T40 Innophy
 *   DDRC_CFG       0x004     0x008
 *   DDRC_CTRL      0x008     0x010
 *   DDRC_LMR       0x00c     0x018
 *   DDRC_REFCNT    0x018     0x038
 *   DDRC_TIMING(n) 0x60+4*n  0x40+8*n   (5 timings, not 6)
 *   DDRC_MMAP0     0x024     0x078
 *   DDRC_MMAP1     0x028     0x080
 */
#define DDRC_STATUS		0x000
#define DDRC_CFG		0x008
#define DDRC_CTRL		0x010
#define DDRC_LMR		0x018
#define DDRC_DLP		0x020
#define DDRC_AUTOSR_EN		0x028
#define DDRC_AUTOSR_CNT		0x030
#define DDRC_REFCNT		0x038
#define DDRC_TIMING(n)		(0x040 + 8 * ((n) - 1))
#define DDRC_MMAP0		0x078
#define DDRC_MMAP1		0x080
#define DDRC_HREGPRO		0x0d8
/* REMAP lives in the APB peripheral region 0xb3012000+0x08; the
 * macro expands to an offset from DDRC_BASE for sdram.c's existing
 * `ddr_writel((DDRC_BASE + offset))` callsite to land at the right
 * absolute address. */
#define DDRC_REMAP(n)		(DDR_APB_BASE - DDRC_BASE + 0x008 + 4 * ((n) - 1))

#define DDRC_DSTATUS_MISS	(1 << 6)
#define DDRC_CTRL_DFI_RST	(1 << 23)
#define DDRC_CTRL_ALH		(1 << 3)
#define DDRC_CTRL_CKE		(1 << 1)

/* Innophy PHY register offsets (added to DDR_PHY_BASE) */
#define INNO_CHANNEL_EN		0x00
#define INNO_MEM_CFG		0x04
#define INNO_TRAINING_CTRL	0x08
#define INNO_CL			0x14
#define INNO_AL			0x18
#define INNO_CWL		0x1c
#define INNO_DQ_WIDTH		0x7c
#define INNO_PLL_FBDIV		0x80
#define INNO_PLL_CTRL		0x84
#define INNO_PLL_PDIV		0x88
#define INNO_WL_DONE		0xc0
#define INNO_PLL_LOCK		0xc8
#define INNO_CALIB_DONE		0xcc
#define INNO_INIT_COMP		0xd0

/* Absolute PHY/APB addresses used by the init sequence */
#define T40_INIT_COMP		(DDR_PHY_BASE + 0xd0)
#define DDR_APB_PHY_INIT	(DDR_APB_BASE + 0x8c)
#define REG_DDR_CTRL		(DDRC_BASE + DDRC_CTRL)
#define REG_DDR_CFG		(DDRC_BASE + DDRC_CFG)
#define REG_DDR_LMR		(DDRC_BASE + DDRC_LMR)

/* CPM offset for the DDR clock divider + DLL reset (same as T31). */
#define CPM_DRCG		0xd0

/*
 * Chip geometry. M14D5121632A: row 13, col 10, 4-bank, 16-bit,
 * single rank.
 */
#define DDR_ROW			13
#define DDR_COL			10
#define DDR_BANK8		0	/* 4-bank */
#define CONFIG_DDR_DW32		0
#define CONFIG_DDR_CS0		1
#define CONFIG_DDR_CS1		0

/* Clock target: DDR = MPLL/2 = 500 MHz. */
#define DDR_MPLL_RATE		1000000000U
#define DDR_TARGET_RATE		(DDR_MPLL_RATE / 2U)

/*
 * GOLD register values. Starting from the T31 DDR2-500 set
 * (M14D5121632A @ 504 MHz) since T40 uses the same DRAM chip
 * and very close clock. Replace per-register from the vendor
 * T40 ddr_dwc.c output when validating on real silicon.
 *
 * TODO: cross-reference with vendor T40 1.3.1 host-tool params
 *       (arch/mips/cpu/xburst2/ddr_dwc.c + ddr_efuse_param.c).
 */
#define DDRC_CFG_VALUE		0x0a288a40
#define DDRC_MMAP0_VALUE	0x000020fc
#define DDRC_MMAP1_VALUE	0x00002400
#define DDRC_CTRL_VALUE		0x0000d91e

#define DDRC_REFCNT_VALUE	0x00f20001
#define DDRC_TIMING1_VALUE	0x040e0806
#define DDRC_TIMING2_VALUE	0x02170707
#define DDRC_TIMING3_VALUE	0x2007051e
#define DDRC_TIMING4_VALUE	0x1a240031
#define DDRC_TIMING5_VALUE	0xff060405
#define DDRC_TIMING6_VALUE	0x32170505
#define DDRP_MR0_VALUE		0x00000f73

#define DDR_CHIP_0_SIZE		67108864	/* 64 MB */
#define DDR_CHIP_1_SIZE		0

/* Total physical RAM size for U-Boot's dram_init(). */
#define T40_DDR_SIZE		DDR_CHIP_0_SIZE

#endif /* __T40_DDR_H__ */
