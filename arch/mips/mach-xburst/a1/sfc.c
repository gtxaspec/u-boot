// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 SPL SFC0 clock + controller bring-up.
 *
 * After the DM-in-SPL DDR init the A1 SPL either hands U-Boot loading to
 * the standard SPL_SPI framework (NOR cold-boot) or returns to the mask
 * ROM (USB-boot). Both need the SFC0 clock re-derived - pll_init()
 * re-rated MPLL - and the controller's GLB/DEV_CONF programmed before
 * the DM SPI driver, or U-Boot proper on USB-boot, touches the flash.
 *
 * A hand-rolled NOR read + LZMA loader used to live here; it is gone now
 * that the NOR path uses the standard SPL_SPI flow.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/a1.h>
#include <mach/t31-sfc.h>

/*
 * SSI cgu entry from the vendor cgu_clk_sel[] table (clk.c): SFC0CDR,
 * source-mux at bits 31:30 (MPLL = 2), ce = bit 29, busy = bit 28,
 * stop = bit 27. These bits differ from the DDR entry; do not guess.
 */
#define CPM_SFC0CDR	0x90
#define SFC_CGU_CE	29
#define SFC_CGU_BUSY	28
#define SFC_CGU_STOP	27

static u32 cpm_readl(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void cpm_writel(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

static void jz_sfc_writel(unsigned int value, unsigned int offset)
{
	writel(value, (void __iomem *)(SFC_BASE + offset));
}

/*
 * Bring up the SFC0 clock + controller. pll_init() re-rated MPLL to
 * 1608 MHz, so SFC0CDR is re-derived: source = MPLL (bits 31:30 = 2),
 * div 80 -> ~20 MHz, comfortably within the controller + NOR spec for
 * the bring-up read. The controller GLB threshold / DEV_CONF delays /
 * clock-gate are then set to the vendor defaults. Called from
 * board_init_f before the NOR SPL_SPI load (or, on USB-boot, before
 * returning to the mask ROM).
 */
void a1_spl_sfc_clk_init(void)
{
	unsigned int tmp;

	/* Ungate SFC0 clock (CLKGR0 bit 24) */
	tmp = cpm_readl(CPM_CLKGR0);
	tmp &= ~CPM_CLKGR0_SFC0;
	cpm_writel(tmp, CPM_CLKGR0);

	{
		u32 reg = cpm_readl(CPM_SFC0CDR);

		reg &= ~((3u << 30) | (3 << SFC_CGU_STOP) | 0xff);
		reg |= (2u << 30) | (1 << SFC_CGU_CE) | 80;
		cpm_writel(reg, CPM_SFC0CDR);
		{ volatile int d = 10000; while (d--); }
	}

	tmp = THRESHOLD << THRESHOLD_OFFSET;
	jz_sfc_writel(tmp, SFC_GLB);

	tmp = CEDL | HOLDDL | WPDL;
	jz_sfc_writel(tmp, SFC_DEV_CONF);

	/* low power consumption */
	jz_sfc_writel(0, SFC_CGE);
}
