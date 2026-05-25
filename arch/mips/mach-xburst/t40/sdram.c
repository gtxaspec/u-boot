// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T40 DDR2 controller and Innophy PHY init (SPL).
 *
 * STUB: not yet implemented. T40 uses the uMCTL2 DDR controller +
 * Innophy DDR PHY (same family as A1 and T32). Vendor reference is
 * the T40-1.3.1 branch of ingenic-u-boot-xburst2 - especially
 * arch/mips/cpu/xburst2/ddr_dwc.c (controller setup), ddr_dqs_training.c
 * (PHY training) and arch/mips/cpu/xburst2/t40/ddr_set_dll.c.
 *
 * QEMU emulates the DDR controller as RAM-already-up (no init needed),
 * so this stub is enough to bring the SPL through to U-Boot proper
 * load in QEMU. Real silicon requires the full vendor sequence.
 */

#include <asm/io.h>
#include <linux/compiler.h>
#include <mach/t40.h>

void sdram_init(void)
{
	/*
	 * TODO: port arch/mips/cpu/xburst2/ddr_dwc.c (uMCTL2 controller
	 * setup) + ddr_dqs_training.c (Innophy PHY soft training) +
	 * ddr_set_dll.c here. QEMU works with this stub (the QEMU model
	 * skips DDR init), real silicon will need the full sequence.
	 */
}
