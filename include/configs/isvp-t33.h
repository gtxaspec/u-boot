/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic ISVP-T33 configuration
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __CONFIG_ISVP_T33_H__
#define __CONFIG_ISVP_T33_H__

/* Memory configuration */
#define CFG_SYS_SDRAM_BASE	0x80000000 /* cached (KSEG0) address */
#define CFG_SYS_INIT_SP_OFFSET	0x400000

/* NS16550-compatible UART, clocked from the 24 MHz EXTAL */
#define CFG_SYS_NS16550_CLK	24000000

#endif /* __CONFIG_ISVP_T33_H__ */
