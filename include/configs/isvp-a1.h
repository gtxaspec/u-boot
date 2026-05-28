/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic ISVP-A1 configuration (XBurst2)
 */

#ifndef __CONFIG_ISVP_A1_H__
#define __CONFIG_ISVP_A1_H__

#define CFG_SYS_SDRAM_BASE	0x80000000
#define CFG_SYS_INIT_SP_OFFSET	0x400000

#define CFG_SYS_NS16550_CLK	24000000

/*
 * SPL pre-relocation malloc arena (SYS_MALLOC_F), used once DM-in-SPL
 * brings up the UCLASS_RAM DDR driver. Sits above SPL BSS
 * (0x80012000 + 0x2000) and below the SPL stack (0x80018000):
 *   0x80001000-0x80012000 : SPL image  (SPL_MAX_SIZE = 0x13000 ceiling)
 *   0x80012000-0x80014000 : SPL BSS     (SPL_BSS_MAX_SIZE = 0x2000)
 *   0x80014000            : SYS_MALLOC_F arena
 *   0x80018000            : SPL stack top (grows down)
 */
#define CFG_MALLOC_F_ADDR	0x80014000

#endif /* __CONFIG_ISVP_A1_H__ */
