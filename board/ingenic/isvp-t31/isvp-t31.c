// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T31 board (DDR2 128 MB, SFC NOR)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <mach/t31.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * MSC0 (microSD) is wired to GPIO port B pins PB0..PB5 in device
 * function 0 (CLK, CMD, D0..D3). The jz_mmc driver assumes a 24 MHz
 * functional clock (jz_mmc_clock_rate()), so divide MPLL (1200 MHz)
 * by 2*(24+1) = 50. The SPL never touches MSC, so set it up here
 * before the driver model probes the controller.
 */
#define GPIO_PORTB_BASE		0xb0011000	/* GPIO_BASE + port B * 0x1000 */
#define G_PXINTC		0x18
#define G_PXMSKC		0x28
#define G_PXPAT1C		0x38
#define G_PXPAT0C		0x48
#define G_PXPUENC		0x118
#define G_PXPDENC		0x128
#define MSC0_PINS		((0x3u << 4) | (0xfu << 0))	/* PB0..PB5 */
#define MSC0_DIV		24				/* 1200/(2*25) = 24 MHz */

static void t31_msc0_init(void)
{
	void __iomem *gpb = (void __iomem *)GPIO_PORTB_BASE;
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	/* Mux PB0..PB5 to device function 0 (vendor gpio_set_func). */
	writel(MSC0_PINS, gpb + G_PXINTC);
	writel(MSC0_PINS, gpb + G_PXMSKC);
	writel(MSC0_PINS, gpb + G_PXPAT1C);
	writel(MSC0_PINS, gpb + G_PXPAT0C);
	writel(MSC0_PINS, gpb + G_PXPUENC);
	writel(MSC0_PINS, gpb + G_PXPDENC);

	/* Ungate the MSC0 functional clock. */
	writel(readl(cpm + CPM_CLKGR0) & ~CPM_CLKGR0_MSC0, cpm + CPM_CLKGR0);

	/* MSC0 clock = MPLL / (2 * (MSC0_DIV + 1)) = 24 MHz. */
	v = MSCCDR_SRC_MPLL | (MSC0_DIV & MSCCDR_DIV_MASK);
	writel(v | MSCCDR_CE, cpm + CPM_MSC0CDR);
	while (readl(cpm + CPM_MSC0CDR) & MSCCDR_BUSY)
		;
	writel(readl(cpm + CPM_MSC0CDR) & ~MSCCDR_CE, cpm + CPM_MSC0CDR);
}

int dram_init(void)
{
	/* DDR2 128 MB; TODO: derive from the DDR controller once it is up */
	gd->ram_size = 128 << 20;
	return 0;
}

int board_init(void)
{
	t31_msc0_init();
	return 0;
}
