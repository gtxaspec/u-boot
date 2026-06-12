/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic ISVP-T32 configuration
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __CONFIG_ISVP_T32_H__
#define __CONFIG_ISVP_T32_H__

/* Memory configuration */
#define CFG_SYS_SDRAM_BASE	0x80000000 /* cached (KSEG0) address */
#define CFG_SYS_INIT_SP_OFFSET	0x400000

/* NS16550-compatible UART, clocked from the 24 MHz EXTAL */
#define CFG_SYS_NS16550_CLK	24000000

/*
 * SPL SRAM layout (mirrors T31; the T32 SRAM window is >= 220 KB,
 * probed write/readback over the mask-ROM USB protocol):
 *   0x80001000-0x80012000 : SPL image  (SPL_MAX_SIZE = 0x13000 ceiling)
 *   0x80012000-0x80014000 : SPL BSS     (SPL_BSS_MAX_SIZE = 0x2000)
 *   0x80014000            : SYS_MALLOC_F arena (SPL_SYS_MALLOC_F_LEN)
 *   0x80018000            : SPL stack top (grows down)
 *
 * board_init_f() reassigns gd = &gdata (a clean BSS gd) for DM-in-SPL,
 * which drops the malloc base the framework reserved below the stack.
 * spl_common_init() restores it from CFG_MALLOC_F_ADDR; without this
 * malloc_simple() allocates from address 0 and the board_init_f DM scan
 * fails with -ENOMEM.
 */
#define CFG_MALLOC_F_ADDR	0x80014000

#endif /* __CONFIG_ISVP_T32_H__ */
