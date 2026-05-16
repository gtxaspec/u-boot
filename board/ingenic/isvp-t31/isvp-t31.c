// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T31 board (DDR2 128 MB, SFC NOR)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	/* DDR2 128 MB; TODO: derive from the DDR controller once it is up */
	gd->ram_size = 128 << 20;
	return 0;
}

int board_init(void)
{
	return 0;
}
