/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic ISVP-T41 configuration (XBurst2)
 */

#ifndef __CONFIG_ISVP_T41_H__
#define __CONFIG_ISVP_T41_H__

#define CFG_SYS_SDRAM_BASE	0x80000000
#define CFG_SYS_INIT_SP_OFFSET	0x400000

#define CFG_SYS_NS16550_CLK	24000000

/*
 * SPL pre-relocation malloc area.
 *
 * SPL runs in on-chip SRAM. Place the SYS_MALLOC_F arena above the SPL
 * binary + BSS, below the stack. Layout for the SFC NOR defconfig:
 *   0x80001000-0x80014000 : SPL image (SPL_MAX_SIZE = 0x13000, 76 KiB)
 *   0x80014000-0x80016000 : SPL BSS   (SPL_BSS_MAX_SIZE = 0x2000)
 *   0x80016000-0x8001a000 : SYS_MALLOC_F (SPL_SYS_MALLOC_F_LEN, 0x4000)
 *   0x8001f000            : SPL stack top (grows down)
 *
 * Post-DRAM malloc (CONFIG_SPL_SYS_MALLOC_SIZE, 4 MiB) sits at
 * CONFIG_SPL_CUSTOM_SYS_MALLOC_ADDR = 0x80800000 in DRAM. The SPL
 * framework's board_init_r() calls mem_malloc_init() to activate
 * that arena before LZMA-decompressing U-Boot proper from NOR.
 * T41 SRAM extends past 0x80020000 (vendor BIG_SPL is 100 KiB), so
 * this is comfortably within bounds.
 */
#define CFG_MALLOC_F_ADDR	0x80016000

#endif /* __CONFIG_ISVP_T41_H__ */
