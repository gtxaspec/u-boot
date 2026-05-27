// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T41 DDR3 controller + Innophy PHY init (SPL) - STAGE 1 STUB
 *
 * This file is a stub for the T41 Stage 1 bring-up (SoC skeleton +
 * USB-boot SPL alive print). T41NQ DDR3 init at 16-bit bus width
 * with the Innophy PHY is Stage 2 work, ported from T40XP's DDR3
 * path with the bus-width adjustment + W631GU6NG chip timings from
 * the vendor `ddr_params_creator` host tool.
 *
 * The stub allows soc.c to call sdram_init() and return without
 * touching the DDR controller. dram_verify() will fail (no DRAM
 * is actually live), which is OK for Stage 1: USB-boot SPL prints
 * "T41 SPL: DDR verify FAILED" then returns to mask ROM. The mask
 * ROM will fail to upload U-Boot proper (no DRAM target), but the
 * SPL has at least proven the bootrom + PLL + UART path on T41NQ
 * silicon.
 */

#include <mach/t41.h>

void sdram_init(void)
{
	/* Stage 1 stub: real DDR3 init lives here in Stage 2. */
}
