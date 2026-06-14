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
 * SYS_MALLOC_F arena base for the DM-in-SPL malloc_simple() heap (DRAM,
 * used after sdram_init()/to_dram()). board_init_f() reassigns gd =
 * &gdata (a clean BSS gd), which drops the malloc base the SPL framework
 * would have reserved below the stack; spl_common_init() restores it from
 * CFG_MALLOC_F_ADDR. Without this, malloc_simple() allocates from address
 * 0 and the board_init_f() DM scan in spl_init() hangs. Same value as
 * T21/T23/T30 (well inside DRAM, used only after DDR is up).
 */
#define CFG_MALLOC_F_ADDR	0x80014000

/* NS16550-compatible UART, clocked from the 24 MHz EXTAL */
#define CFG_SYS_NS16550_CLK	24000000

#endif /* __CONFIG_ISVP_T20_H__ */
