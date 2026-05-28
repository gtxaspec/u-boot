/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T40 DDR controller and Innophy PHY register map.
 *
 * Per-variant DDR values (controller, PHY, mode register, geometry,
 * clocks) live in <mach/t40n-ddr.h> or <mach/t40xp-ddr.h>; this file
 * carries only the structural pieces that are the same across all
 * T40 variants (register offsets, controller and PHY base addresses,
 * field-bit definitions used by sdram.c).
 *
 * T40 uses the legacy Ingenic "Innophy" DDR controller (DDRC_BASE
 * 0xb34f0000) paired with the Innophy training PHY (DDR_PHY_BASE
 * 0xb3011000) and the DDR APB peripheral region (DDR_APB_BASE
 * 0xb3012000). The register layout matches vendor
 * `arch/mips/include/asm/ddr_innophy.h` from the T40 U-Boot 2013
 * source. Do NOT swap to the Synopsys DWC layout the T31 port used;
 * Innophy decodes the same numerical register values into different
 * geometry/timing.
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

#ifndef __T40_DDR_H__
#define __T40_DDR_H__

#include <mach/t40.h>		/* DDRC_BASE / DDR_PHY_BASE / CPM_BASE */

#define DDR_APB_BASE		0xb3012000

/* DDRC register offsets (Innophy layout). */
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

/* Vendor T40 DFI init: APB region DWCFG (offset 0) + DWSTATUS (offset 4) */
#define DDRC_DWCFG		(DDR_APB_BASE + 0x00)
#define DDRC_DWSTATUS		(DDR_APB_BASE + 0x04)
#define DDRC_DWCFG_DFI_INIT_START	(1 << 3)
#define DDRC_DWSTATUS_DFI_INIT_COMP	(1 << 0)

/* CPM offset for the DDR clock divider + DLL reset. */
#define CPM_DRCG		0xd0

/* Per-variant DDR values: geometry, clocks, controller VALUEs,
 * PHY VALUEs, MR0..3, REMMAP_ARRAY. Vendor T40-1.3.1 has no separate
 * T40NN target; T40N and T40NN share the same DDR2-500 setpoint. */
#if defined(CONFIG_T40_VARIANT_T40XP)
#include <mach/t40xp-ddr.h>
#elif defined(CONFIG_T40_VARIANT_T40A)
#include <mach/t40a-ddr.h>
#else /* T40_VARIANT_T40N or T40_VARIANT_T40NN */
#include <mach/t40n-ddr.h>
#endif

/* Compat alias for code still referencing the old single name. */
#define DDRP_MR0_VALUE		DDR_MR0_VALUE

/* Total physical RAM size for U-Boot's dram_init(). */
#define T40_DDR_SIZE		DDR_CHIP_0_SIZE

#endif /* __T40_DDR_H__ */
