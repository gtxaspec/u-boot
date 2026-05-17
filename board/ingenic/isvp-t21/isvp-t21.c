// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T21 board (DDR2 64 MB, SFC NOR)
 *
 * Stage 1 of the T21 port: minimal U-Boot-proper board glue. The
 * SPL (mach-xburst/t21) brings up console + PLL; with
 * CONFIG_SPL_T21_USB_BOOT it returns to the mask ROM. DDR, SFC
 * NOR-boot and USB land in later stages.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <stdio.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	gd->ram_size = 64 << 20;	/* T21: DDR2 M14D5121632A 64 MB */
	return 0;
}

int board_init(void)
{
	return 0;
}

/* Printed right after the "Model:" line; shows the exact T21 SKU. */
int checkboard(void)
{
	printf("Variant: %s\n", CONFIG_T21_VARIANT_NAME);
	return 0;
}
