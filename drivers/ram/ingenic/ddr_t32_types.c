// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 (PRJ007) DDR - per-variant uMCTL2/PHY/clock tables.
 *
 * Verbatim vendor host-tool output (ddr_creator_chip_v3 +
 * pll_params_creator) for each PRJ007_<sku>_goat class, from Ingenic
 * SDK Tassadar-T32/T33-2.1.0.0-20260203 - NOT recomputed. One struct
 * per distinct param set, selected by the &ddr node's per-SKU
 * compatible (ingenic,t32<sku>-ddr-innophy) through the driver
 * of_match .data at probe (and by ingenic_t32_ddr_pll_setpoints() in
 * SPL). Param-identical vendor bin badges share a struct: VL/ZL = LQ,
 * ZN = VN, ZX = VX. This is the single source for the per-variant
 * values; the former mach/t32-ddr.h #if chain was folded in here.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <linux/types.h>

#include "ddr_t32.h"

/*
 * T32LQ (also sold as T32VL/T32ZL): DDR2 M14D5121632A, 64 MB, 16-bit,
 * CS0 only, DDR 600 MT/s (CK 300 MHz), soft-train. APLL 900 / MPLL
 * 1200. An earlier snapshot had ADDRMAP1=0x003f1515 (older creator
 * output) and PLLPOSTDIV(EN)=0; on real T32LQ silicon the PHY
 * DQS-gating calibration BNE_INNOPHY_REG(calib_end, 0x1) spun forever
 * without the PHY post-divider enabled, so those values follow the
 * 20260203 SDK output.
 */
const struct ingenic_t32_ddr_variant ingenic_t32_ddr_variant_t32lq = {
	.name		= "T32LQ",
	.chip		= "M14D5121632A",
	.type		= T32_DDR_TYPE_DDR2,
	.cpu_mhz	= 900,

	.size		= 0x04000000,		/* 64 MB */
	.ddr_ck_hz	= 300000000,		/* 600 MT/s / 2 */

	/* APLL 900 (CPU), MPLL 1200 (DDR/bus); CPCCR H0/H2DIV 5, PDIV 11. */
	.cpapcr		= 0x04b05101,
	.cpmpcr		= 0x06405101,
	.cpccr_div	= 0x557b5510,
	.cpccr_sel	= 0x9a7b5510,

	.mstr		= 0x00040000,
	.init0		= 0x0001003b,
	.init1		= 0x00000007,
	.init3		= 0x01130000,
	.init4		= 0x00000000,
	.init5		= 0x00000000,
	.timing0	= 0x0a01140e,
	.timing1	= 0x00020312,
	.timing2	= 0x00000408,
	.timing3	= 0x00001000,
	.timing4	= 0x04010405,
	.timing5	= 0x01010202,
	.timing6	= 0x00000000,
	.timing7	= 0x00000101,
	.timing8	= 0x00000402,
	.timing14	= 0x00000000,
	.addrmap1	= 0x003f0808,
	.addrmap2	= 0x00000000,
	.addrmap3	= 0x00000000,
	.addrmap4	= 0x00001f1f,
	.addrmap5	= 0x06060606,
	.addrmap6	= 0x0f0f0f06,
	.rfshctl3	= 0x00000002,
	.rfshtmg	= 0x00490020,
	.odtcfg		= 0x07020708,
	.dfitmg0	= 0x06020002,
	.dfitmg1	= 0x00080307,
	.dfiupd0	= 0x80400003,

	.phy_channel_en	= 0x00000003,
	.phy_cwl	= 0x00000007,
	.phy_cl		= 0x00000008,
	.phy_al		= 0x00000000,
	.phy_trfc	= 0x00000020,
	.phy_trefi	= 0x00000925,
	.phy_mem_sel	= 0x00000000,
	.phy_pllpostdiven = 0x00000001,
	.phy_pllpostdiv	= 0x00000001,
};
