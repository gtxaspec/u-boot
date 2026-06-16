/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic ISVP-T20 configuration
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __CONFIG_ISVP_T20_H__
#define __CONFIG_ISVP_T20_H__

/* Memory configuration */
#define CFG_SYS_SDRAM_BASE	0x80000000 /* cached (KSEG0) address */
#define CFG_SYS_INIT_SP_OFFSET	0x400000

/*
 * SYS_MALLOC_F arena base for the DM malloc_simple() heap.
 *
 * The XBurst start.S sets up neither the stack (it keeps the mask-ROM sp) nor
 * gd; the C board_init_f() reassigns gd = &gdata (a clean BSS gd), which drops
 * the malloc base the SPL framework would otherwise reserve below the stack,
 * so spl_common_init() restores gd->malloc_base from CFG_MALLOC_F_ADDR when it
 * is defined. Without this the single-stage SPL's malloc_simple() allocates
 * from address 0 and the board_init_f() DM scan in spl_init() hangs. Same
 * value as T21/T23/T30 (well inside DRAM, used only after DDR is up).
 *
 * The TPL must NOT define it: the TPL runs cache-as-RAM before any DRAM and
 * T20's Synopsys-DWC controller hangs on any pre-init DRAM access, so its
 * board_init_f() points gd->malloc_base at a cache-window BSS pool instead -
 * leaving CFG_MALLOC_F_ADDR unset keeps spl_common_init() from re-pointing the
 * heap into (phantom) DRAM.
 */
#ifndef CONFIG_TPL_BUILD
#define CFG_MALLOC_F_ADDR	0x80014000
#endif

/* NS16550-compatible UART, clocked from the 24 MHz EXTAL */
#define CFG_SYS_NS16550_CLK	24000000

#endif /* __CONFIG_ISVP_T20_H__ */
