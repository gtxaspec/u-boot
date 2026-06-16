/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T20 (XBurst1) DDR2 controller + Synopsys DWC PHY register map
 * and per-SKU variant table.
 *
 * T20 and T10 are the odd ones out of the XBurst1 camera SoCs: they do NOT use
 * the Innophy DDR2 core that T21/T23/T30/T31 share through ddr_t31.c. They have
 * the Synopsys DesignWare (DWC) DDR PHY (JZ4775/JZ4780 generation, vendor
 * arch/mips/cpu/xburst/ddr_dwc.c + ddr_set_dll.c) with hardware ZQ impedance
 * calibration and a hardware DQS training engine - a materially different (and
 * longer) init sequence, so the DWC SoCs share this driver/struct (named after
 * the representative T20, the same way the Innophy four share ddr_t31.c) rather
 * than the Innophy one. The bring-up sequence is identical for T10 and T20; only
 * the per-SKU parameter values differ, and they all come from the DT.
 *
 * The per-SKU values (geometry, clock setpoints, ddr_params_creator GOLD
 * register words) come from the devicetree: the &ddr node carries an
 * "ingenic,sdram-params" u32 array (struct ingenic_t20_ddr_params,
 * serialized) under a single compatible "ingenic,t20-ddr-dwc". No
 * compile-time CONFIG_T20_VARIANT_*, no per-SKU compatible/.data - one
 * binding per SoC, and the board DT picks the part. This mirrors the
 * mainline rk3328 DMC (rockchip,sdram-params) so the same driver works
 * under OF_CONTROL and, later, OF_PLATDATA (TPL). Register map and the
 * clock-invariant GOLD words ported verbatim from the former
 * arch/mips/mach-xburst/include/mach/t20-ddr.h.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef _DRIVERS_RAM_INGENIC_DDR_T20_H
#define _DRIVERS_RAM_INGENIC_DDR_T20_H

#include <linux/types.h>

/* Base addresses (KSEG1 uncached); fixed on XBurst1 T20. */
#define DDRC_BASE		0xb34f0000
#define DDR_PHY_BASE		0xb3011000	/* DDRC_BASE + DDR_PHY_OFFSET */

/* CPM DDR clock-gate register (CPM_DDRCDR comes from <mach/t20.h>). */
#define CPM_DRCG		0xd0

/* DDR controller register offsets (relative to DDRC_BASE) */
#define DDRC_STATUS		0x000
#define DDRC_CFG		0x004
#define DDRC_CTRL		0x008
#define DDRC_LMR		0x00c
#define DDRC_REFCNT		0x018
#define DDRC_MMAP0		0x024
#define DDRC_MMAP1		0x028
#define DDRC_DLP		0x0bc
#define DDRC_AUTOSR_EN		0x304
#define DDRC_TIMING(n)		(0x060 + 4 * ((n) - 1))
#define DDRC_REMAP(n)		(0x09c + 4 * ((n) - 1))

#define DDRC_DSTATUS_MISS	(1 << 6)
#define DDRC_CFG_CS1EN		(1 << 7)
#define DDRC_CFG_CS0EN		(1 << 6)
#define DDRC_CTRL_ALH		(1 << 3)
#define DDRC_CTRL_CKE		(1 << 1)

/* DWC PHY register offsets (relative to DDR_PHY_BASE) */
#define DDRP_PIR		0x004	/* PHY Initialization Register */
#define DDRP_PGCR		0x008	/* PHY General Configuration */
#define DDRP_PGSR		0x00c	/* PHY General Status */
#define DDRP_DLLGCR		0x010	/* DLL General Control */
#define DDRP_ACDLLCR		0x014	/* AC DLL Control */
#define DDRP_PTR0		0x018	/* PHY Timing Register 0 */
#define DDRP_PTR1		0x01c	/* PHY Timing Register 1 */
#define DDRP_PTR2		0x020	/* PHY Timing Register 2 */
#define DDRP_ACIOCR		0x024	/* AC I/O Configuration */
#define DDRP_DXCCR		0x028	/* DATX8 Common Configuration */
#define DDRP_DSGCR		0x02c	/* DDR System General Config */
#define DDRP_DCR		0x030	/* DRAM Configuration */
#define DDRP_DTPR0		0x034	/* DRAM Timing Parameters 0 */
#define DDRP_DTPR1		0x038	/* DRAM Timing Parameters 1 */
#define DDRP_DTPR2		0x03c	/* DRAM Timing Parameters 2 */
#define DDRP_MR0		0x040	/* Mode Register 0 */
#define DDRP_MR1		0x044	/* Mode Register 1 */
#define DDRP_MR2		0x048	/* Mode Register 2 */
#define DDRP_MR3		0x04c	/* Mode Register 3 */
#define DDRP_ODTCR		0x050	/* ODT Configure Register */
#define DDRP_DTAR		0x054	/* Data Training Address */
#define DDRP_DXGCR(n)		(0x1c0 + (n) * 0x40)	/* DATX8 n Gen Cfg */
#define DDRP_DXGSR0(n)		(0x1c4 + (n) * 0x40)	/* DATX8 n Gen Sts */
#define DDRP_ZQXCR0(n)		(0x180 + (n) * 0x10)	/* ZQ Imp Ctrl 0 */
#define DDRP_ZQXCR1(n)		(0x184 + (n) * 0x10)	/* ZQ Imp Ctrl 1 */
#define DDRP_ZQXSR0(n)		(0x188 + (n) * 0x10)	/* ZQ Imp Status 0 */

/* DDRP PHY Initialization Register bits */
#define DDRP_PIR_INIT		(1 << 0)
#define DDRP_PIR_DLLSRST	(1 << 1)
#define DDRP_PIR_DLLLOCK	(1 << 2)
#define DDRP_PIR_ZCAL		(1 << 3)
#define DDRP_PIR_ITMSRST	(1 << 4)
#define DDRP_PIR_DRAMRST	(1 << 5)
#define DDRP_PIR_DRAMINT	(1 << 6)
#define DDRP_PIR_QSTRN		(1 << 7)
#define DDRP_PIR_DLLBYP		(1 << 17)
#define DDRP_PIR_LOCKBYP	(1 << 29)

/* DDRP PHY General Status Register bits */
#define DDRP_PGSR_IDONE		(1 << 0)
#define DDRP_PGSR_DLDONE	(1 << 1)
#define DDRP_PGSR_ZCDONE	(1 << 2)
#define DDRP_PGSR_DIDONE	(1 << 3)
#define DDRP_PGSR_DTDONE	(1 << 4)
#define DDRP_PGSR_DTERR		(1 << 5)
#define DDRP_PGSR_DTIERR	(1 << 6)

#define DDRP_ZQXCR_ZDEN		(1 << 28)
#define DDRP_DXGCR_DXEN		(1 << 0)

/*
 * DWC GOLD values that are identical across every SKU served by this driver
 * (T20 64M/128M and T10) - kept as driver constants. Values that differ between
 * SKUs (geometry/timing, plus REFCNT/MR0/PTR0/PTR1 and the DDR clock divider,
 * which differ on T10's 400 MHz part vs T20's 500 MHz) live in the per-SKU
 * "ingenic,sdram-params" array instead.
 */
#define DDRC_CTRL_VALUE		0x0000d91e
#define DDRC_AUTOSR_EN_VALUE	0x00000000
#define DDRP_MR1_VALUE		0x00000002
#define DDRP_MR2_VALUE		0x00000000
#define DDRP_MR3_VALUE		0x00000000
#define DDRP_PTR2_VALUE		0x00000000
#define DDRP_DTPR2_VALUE	0x1001a8c8
#define DDRP_PGCR_VALUE		0x01842e02
#define DDRP_ODTCR_VALUE	0x00000000
#define DDRP_DX0GCR_VALUE	0x00090881
#define DDRP_DX1GCR_VALUE	0x00090881
#define DDRP_DX2GCR_VALUE	0x00090e80
#define DDRP_DX3GCR_VALUE	0x00090e80
#define DDRP_ZQNCR1_VALUE	0x0000006b

/* {0:cal_value, 1:req_value} for the ZQ impedance recompute (shared). */
#define DDRP_IMPANDCE_ARRAY	{ 0x00009c40, 0x00009c40 }
#define DDRP_ODT_IMPANDCE_ARRAY	{ 0x0000c350, 0x0000c350 }
#define DDRP_RZQ_TABLE \
	{ 0x00, 0x01, 0x02, 0x03, 0x06, 0x07, 0x04, 0x05, \
	  0x0c, 0x0d, 0x0e, 0x0f, 0x0a, 0x0b, 0x08, 0x09, \
	  0x18, 0x19, 0x1a, 0x1b, 0x1e, 0x1f, 0x1c, 0x1d, \
	  0x14, 0x15, 0x16, 0x17, 0x12, 0x13, 0x10, 0x11 }

/*
 * Per-SKU DDR configuration, deserialized from the &ddr node's
 * "ingenic,sdram-params" devicetree property (a flat u32 array). The field
 * order below IS the array order, and the struct is all-u32 so it can be
 * filled in one shot by casting it to (u32 *) - the mainline rk3328
 * rockchip,sdram-params idiom (dev_read_u32_array for OF_CONTROL, an
 * fdt32 parse for the pre-DM bring-up, dtoc-baked platdata for OF_PLATDATA).
 * The DWC GOLD words are direct vendor ddr_params_creator output; the words
 * shared by every SKU stay as driver constants above, only the per-part
 * values are carried here, plus the SPL PLL setpoints.
 *
 * [0..2] are the SPL PLL setpoints (CPAPCR/CPMPCR M/N/OD1/OD0 + the CPCCR
 * divider): the TPL's UCLASS_RAM probe feeds them to the SoC's pll_init_params()
 * (t20/pll.c or t10/pll.c) before bringing DDR up (the DWC controller hangs on
 * any pre-DDR DRAM access, so PLL + DDR come up first). T20 runs MPLL 1000 /
 * DDR 500 (cdr 1); T10 runs MPLL 1200 / DDR 400 (cdr 2); the geometry/timing and
 * the REFCNT/MR0/PTR0/PTR1 words differ per part - all carried in the array.
 */
struct ingenic_t20_ddr_params {
	u32 apll_mnod;			/* [0]  CPAPCR M/N/OD1/OD0 (CPU/APLL) */
	u32 mpll_mnod;			/* [1]  CPMPCR M/N/OD1/OD0 (DDR/MPLL) */
	u32 cpccr;			/* [2]  CPCCR clock-divider word */
	u32 chip0_size;			/* [3]  total DRAM bytes */
	u32 ddrc_cfg;			/* [4]  DDRC_CFG */
	u32 ddrc_mmap0;			/* [5]  DDRC_MMAP0 */
	u32 ddrc_mmap1;			/* [6]  DDRC_MMAP1 */
	u32 ddrc_timing[6];		/* [7..12]  DDRC_TIMING1..6 */
	u32 ddrp_dcr;			/* [13] DWC PHY DCR */
	u32 ddrp_dtpr0;			/* [14] DWC PHY DTPR0 */
	u32 ddrp_dtpr1;			/* [15] DWC PHY DTPR1 */
	u32 remap[5];			/* [16..20] DDRC_REMAP1..5 */
	u32 ddr_cdr;			/* [21] CPM_DDRCDR divider (DDR = MPLL/(cdr+1)) */
	u32 ddrc_refcnt;		/* [22] DDRC_REFCNT */
	u32 ddrp_mr0;			/* [23] DWC PHY MR0 */
	u32 ddrp_ptr0;			/* [24] DWC PHY PTR0 */
	u32 ddrp_ptr1;			/* [25] DWC PHY PTR1 */
};

#endif /* _DRIVERS_RAM_INGENIC_DDR_T20_H */
