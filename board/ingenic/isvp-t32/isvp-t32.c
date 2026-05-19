// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T32 board (PRJ007, DDR2 M14D5121632A target)
 *
 * U-Boot-proper board glue. The SPL (mach-xburst/t32) brings up
 * console + PLL (Stage 1); Innophy DDR2 + the real RAM size land
 * with Stage 2. Forward-ported from the vendor U-Boot 2022.10 PRJ
 * (PRJ007 = T32).
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <stdio.h>
#include <asm/global_data.h>
#include <mach/t32.h>
#include <mach/t32-ddr.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	/* Per the Kconfig DDR class: 64 MB DDR2 / 128 MB DDR3 /
	 * 256 MB DDR3-W632 / 128 MB LPDDR3. */
	gd->ram_size = T32_DDR_SIZE;
	return 0;
}

int board_init(void)
{
	return 0;
}

/* Printed right after the "Model:" line; shows the exact T32 SKU. */
int checkboard(void)
{
	printf("Variant: %s\n", CONFIG_T32_VARIANT_NAME);
#ifdef CONFIG_SPL_T32_USB_BOOT
	puts("Loader: USB-boot (development; no NOR access)\n");
#endif
	return 0;
}
