// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T23 board (DDR2 128 MB, SFC NOR)
 *
 * Stage 1 of the T23 port: minimal U-Boot-proper board glue. The
 * SPL (mach-xburst/t23) brings up console + PLL and returns to the
 * mask ROM (CONFIG_SPL_T23_USB_BOOT); DDR/SFC and the full board
 * bring-up land in later stages.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <stdio.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	/* DDR2 128 MB; derive from the DDR controller once it is up. */
	gd->ram_size = 128 << 20;
	return 0;
}

int board_init(void)
{
	return 0;
}

/* Printed right after the "Model:" line; shows the exact T23 SKU. */
int checkboard(void)
{
	printf("Variant: %s\n", CONFIG_T23_VARIANT_NAME);
	return 0;
}
