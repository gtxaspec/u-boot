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

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	/* Placeholder until Stage 2 (Innophy DDR2) lands. */
	gd->ram_size = 64 << 20;
	return 0;
}

int board_init(void)
{
	return 0;
}
