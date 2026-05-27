// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T41 DDR3 controller + Innophy PHY init (SPL)
 *
 * T41 uses a DIFFERENT Innophy PHY revision from T40 - the register
 * map is completely reshuffled (PLL_FBDIV at 0x140 vs T40's 0x80,
 * PLL_LOCK at 0x180 vs T40's 0xc8, etc.). T40's sdram.c cannot be
 * reused with value substitutions - the register addresses themselves
 * are wrong.
 *
 * The T41 vendor DDR init lives in:
 *   arch/mips/cpu/xburst2/ddr_innophy.c (T41-1.2.6 branch)
 *   arch/mips/include/asm/ddr_innophy.h (T41 PHY register offsets)
 *
 * Key T41 PHY register offsets (from vendor ddr_innophy.h):
 *   INNO_PHY_RST    = 0x000    (T40: 0x00)
 *   MEM_CFG         = 0x004    (T40: 0x04)
 *   TRAINING_CTRL   = 0x008    (T40: 0x08)
 *   CL              = 0x014    (T40: 0x14)
 *   AL              = 0x018    (T40: 0x18)
 *   CWL             = 0x01c    (T40: 0x1c)
 *   DQ_WIDTH        = 0x034    (T40: 0x7c)  <-- different
 *   PLL_FBDIVL      = 0x140    (T40: 0x80)  <-- different
 *   PLL_FBDIVH      = 0x144    (T40: n/a)   <-- new
 *   PLL_PDIV        = 0x148    (T40: 0x88)  <-- different
 *   PLL_CTRL        = 0x14c    (T40: 0x84)  <-- different
 *   PLL_LOCK        = 0x180    (T40: 0xc8)  <-- different
 *   CALIB_DONE      = 0x184    (T40: 0xcc)  <-- different
 *
 * T41NQ DDR controller register values (from ddr_params_creator)
 * are correct and live in <mach/t41nq-ddr.h>. Only the PHY init
 * sequence needs porting from the vendor source.
 *
 * Stage 2 TODO: faithful port of vendor ddr_phy_init() +
 * ddrc_dfi_init() + ddrp_set_drv_odt() + sdram_init() using the
 * T41 PHY register map above.
 */

#include <mach/t41.h>

void sdram_init(void)
{
}
