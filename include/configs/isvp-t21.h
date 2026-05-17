/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic ISVP-T21 configuration
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __CONFIG_ISVP_T21_H__
#define __CONFIG_ISVP_T21_H__

/* Memory configuration */
#define CFG_SYS_SDRAM_BASE	0x80000000 /* cached (KSEG0) address */
#define CFG_SYS_INIT_SP_OFFSET	0x400000

/* NS16550-compatible UART, clocked from the 24 MHz EXTAL */
#define CFG_SYS_NS16550_CLK	24000000

#endif /* __CONFIG_ISVP_T21_H__ */
