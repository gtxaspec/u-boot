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
 * binary + BSS + stack: per the defconfig layout the SPL image lives
 * 0x80001000-0x80012000 (SPL_MAX_SIZE = 0x11000), BSS 0x80012000-
 * 0x80014000 (SPL_BSS_MAX_SIZE = 0x2000), stack at 0x80014000 growing
 * down. SYS_MALLOC_F at 0x80014000 with SPL_SYS_MALLOC_F_LEN
 * (default 0x2000) lives between BSS end and the stack base. T41 SRAM
 * extends past 0x80019000 (vendor BIG_SPL is 100 KiB), so this is
 * comfortably within bounds.
 */
#define CFG_MALLOC_F_ADDR	0x80014000

#endif /* __CONFIG_ISVP_T41_H__ */
