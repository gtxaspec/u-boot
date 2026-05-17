// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T23 board (DDR2, SFC NOR)
 *
 * Minimal U-Boot-proper board glue. The SPL (mach-xburst/t23)
 * brings up console + PLL + Innophy DDR2; with CONFIG_SPL_T23_
 * USB_BOOT it returns to the mask ROM. T23/T23N = 64 MB,
 * T23DL/T23DN = 32 MB (CONFIG_T23_DRAM_32M); no 128 MB board.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <stdio.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
#if defined(CONFIG_T23_DRAM_32M)
	gd->ram_size = 32 << 20;	/* T23DL/T23DN: M14D2561616A */
#else
	gd->ram_size = 64 << 20;	/* T23/T23N: M14D5121632A */
#endif
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
