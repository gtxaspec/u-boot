/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic ISVP-T30 configuration
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __CONFIG_ISVP_T30_H__
#define __CONFIG_ISVP_T30_H__

/* Memory configuration */
#define CFG_SYS_SDRAM_BASE	0x80000000 /* cached (KSEG0) address */
#define CFG_SYS_INIT_SP_OFFSET	0x400000

/* NS16550-compatible UART, clocked from the 24 MHz EXTAL */
#define CFG_SYS_NS16550_CLK	24000000

/*
 * SYS_MALLOC_F arena base for the DRAM-resident SPL's DM malloc_simple() heap.
 * The TPL brings up DDR in cache-as-RAM and loads this SPL into real DRAM, so
 * by board_init_f() these are real DRAM addresses (image at SPL_TEXT_BASE, BSS
 * at SPL_BSS_START_ADDR, this arena, stack at SPL_STACK).
 *
 * The XBurst start.S sets up neither the stack (it keeps the mask-ROM sp) nor
 * gd; board_init_f() reassigns gd = &gdata (a clean BSS gd), dropping the
 * malloc base the SPL framework reserved below the stack, so spl_common_init()
 * restores gd->malloc_base from CFG_MALLOC_F_ADDR. Without it malloc_simple()
 * allocates from address 0 and the DM scan hangs. Same value as T20/T21/T23.
 *
 * The TPL must NOT define it: it runs cache-as-RAM before any DRAM, so its
 * board_init_f() points gd->malloc_base at a cache-window BSS pool instead -
 * leaving CFG_MALLOC_F_ADDR unset keeps spl_common_init() from re-pointing the
 * heap into (phantom) pre-DDR DRAM.
 */
#ifndef CONFIG_TPL_BUILD
#define CFG_MALLOC_F_ADDR	0x80014000
#endif

#endif /* __CONFIG_ISVP_T30_H__ */
