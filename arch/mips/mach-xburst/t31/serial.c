// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal Ingenic T31 SPL UART
 *
 * The SPL runs from on-chip SRAM with no driver model, so it uses a
 * tiny direct-register 16550 driver for the bring-up console. Full
 * U-Boot uses the DM ns16550 driver. The UART is clocked from the
 * 24 MHz EXTAL before pll_init(), matching the vendor SPL.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t31.h>

/* 16550 register numbers; the T31 UART uses a 4-byte register stride. */
#define UART_THR	0
#define UART_DLL	0
#define UART_IER	1
#define UART_DLM	1
#define UART_FCR	2
#define UART_LCR	3
#define UART_LSR	5

#define UART_FCR_FE	0x01		/* FIFO enable */
#define UART_FCR_UME	0x10		/* Ingenic UART module enable */
#define UART_LCR_DLAB	0x80
#define UART_LCR_8N1	0x03
#define UART_LSR_THRE	0x20		/* TX holding register empty */

#define T31_UART1_CLK	24000000	/* EXTAL feeds the baud generator */
#define T31_UART1_BAUD	115200

static void __iomem *uart_reg(unsigned int r)
{
	return (void __iomem *)(UART1_BASE + (r << 2));
}

void t31_spl_serial_init(void)
{
	unsigned int div = T31_UART1_CLK / (16 * T31_UART1_BAUD);

	writel(UART_FCR_UME | UART_FCR_FE, uart_reg(UART_FCR));
	writel(UART_LCR_DLAB | UART_LCR_8N1, uart_reg(UART_LCR));
	writel(div & 0xff, uart_reg(UART_DLL));
	writel((div >> 8) & 0xff, uart_reg(UART_DLM));
	writel(UART_LCR_8N1, uart_reg(UART_LCR));
	writel(0, uart_reg(UART_IER));
}

void t31_spl_putc(char c)
{
	if (c == '\n')
		t31_spl_putc('\r');
	while (!(readl(uart_reg(UART_LSR)) & UART_LSR_THRE))
		;
	writel(c, uart_reg(UART_THR));
}

void t31_spl_puts(const char *s)
{
	while (*s)
		t31_spl_putc(*s++);
}
