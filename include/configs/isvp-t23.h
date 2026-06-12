/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic ISVP-T23 configuration
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __CONFIG_ISVP_T23_H__
#define __CONFIG_ISVP_T23_H__

/* Memory configuration */
#define CFG_SYS_SDRAM_BASE	0x80000000 /* cached (KSEG0) address */
#define CFG_SYS_INIT_SP_OFFSET	0x400000

/* NS16550-compatible UART, clocked from the 24 MHz EXTAL */
#define CFG_SYS_NS16550_CLK	24000000

/*
 * SPL memory layout. T23 boots cache-as-RAM: there is NO backed memory
 * until the SPL itself brings DDR up, and the whole pre-DDR budget is
 * the 16 KB L1 + 64 KB L2 (~80 KB) - only the cache-resident footprint
 * (image + BSS + the live top of the stack) exists. board_init_f()
 * inits DDR imperatively and makes the SPL DRAM-resident (image
 * re-read from NOR, live data copied from the cache) before
 * spl_init(), so by the time anything allocates from the malloc-f
 * arena these are real DRAM addresses:
 *   0x80001000-0x80012000 : SPL image  (SPL_MAX_SIZE = 0x13000 ceiling)
 *   0x80012000-0x80014000 : SPL BSS     (SPL_BSS_MAX_SIZE = 0x2000)
 *   0x80014000-0x80024000 : SYS_MALLOC_F arena (SPL_SYS_MALLOC_F_LEN 64 KB)
 *   0x80080000            : SPL stack top (grows down; far above the heap)
 *
 * board_init_f() reassigns gd = &gdata (a clean BSS gd) for DM-in-SPL,
 * which drops the malloc base the framework reserved below the stack.
 * spl_common_init() restores it from CFG_MALLOC_F_ADDR; without this
 * malloc_simple() allocates from address 0 and the board_init_f DM scan
 * fails with -ENOMEM.
 */
#define CFG_MALLOC_F_ADDR	0x80014000

#endif /* __CONFIG_ISVP_T23_H__ */
