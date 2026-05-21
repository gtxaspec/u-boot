// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-A1 board (DDR3 256 MB, SFC NOR)
 */

#include <init.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <mach/a1.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	gd->ram_size = 256 << 20;
	return 0;
}

int board_init(void)
{
	return 0;
}

int checkboard(void)
{
	puts("Board: Ingenic ISVP-A1 (XBurst2)\n");
	return 0;
}
