// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal Ingenic T31 SPL UART (UART1 console)
 *
 * Faithful transliteration of the vendor jz_serial driver: the
 * Ingenic UART must be taken out of SIR/IrDA mode (ISR register) and
 * brought up in a specific order, otherwise it produces no output on
 * real silicon (QEMU does not model SIR mode, so a simpler sequence
 * appears to work there but fails on hardware). The SPL runs with no
 * driver model; full U-Boot uses the DM ns16550 driver. The UART is
 * clocked from the 24 MHz EXTAL before pll_init(), like the vendor.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t31.h>

/* jz_uart byte registers, 4-byte stride (see vendor asm/jz_uart.h) */
#define U_THR_DLL	0x00
#define U_IER_DLH	0x04
#define U_FCR		0x08
#define U_LCR		0x0c
#define U_LSR		0x14
#define U_ISR		0x20

#define FCR_FE		(1 << 0)	/* FIFO enable */
#define FCR_RFLS	(1 << 1)	/* flush RX FIFO */
#define FCR_TFLS	(1 << 2)	/* flush TX FIFO */
#define FCR_UUE		(1 << 4)	/* UART unit enable */
#define LCR_WLEN_8	(3 << 0)
#define LCR_STOP_1	(0 << 2)
#define LCR_DLAB	(1 << 7)
#define LSR_TDRQ	(1 << 5)	/* TX FIFO half-empty */
#define LSR_TEMT	(1 << 6)	/* TX FIFO + shift reg empty */
#define SIRCR_TSIRE	(1 << 0)	/* 1: TX in IrDA mode */
#define SIRCR_RSIRE	(1 << 1)	/* 1: RX in IrDA mode */

#define T31_UART1_CLK	24000000	/* EXTAL feeds the baud generator */
#define T31_UART1_BAUD	115200

static void u_wb(u8 v, unsigned int off)
{
	writeb(v, (void __iomem *)(UART1_BASE + off));
}

static u8 u_rb(unsigned int off)
{
	return readb((void __iomem *)(UART1_BASE + off));
}

void t31_spl_serial_init(void)
{
	unsigned int div = T31_UART1_CLK / 16 / T31_UART1_BAUD;
	u8 lcr;

	u_wb(0, U_IER_DLH);				/* disable interrupts */
	u_wb((u8)~FCR_UUE, U_FCR);			/* disable UART unit */
	u_wb((u8)~(SIRCR_RSIRE | SIRCR_TSIRE), U_ISR);	/* UART mode, not SIR */
	u_wb(LCR_WLEN_8 | LCR_STOP_1, U_LCR);		/* 8N1 */

	lcr = u_rb(U_LCR) | LCR_DLAB;
	u_wb(lcr, U_LCR);
	u_wb((div >> 8) & 0xff, U_IER_DLH);		/* divisor latch high */
	u_wb(div & 0xff, U_THR_DLL);			/* divisor latch low */
	lcr &= ~LCR_DLAB;
	u_wb(lcr, U_LCR);

	/* enable UART unit, enable + flush FIFOs */
	u_wb(FCR_UUE | FCR_FE | FCR_TFLS | FCR_RFLS, U_FCR);
}

void t31_spl_putc(char c)
{
	if (c == '\n')
		t31_spl_putc('\r');
	u_wb((u8)c, U_THR_DLL);
	while ((u_rb(U_LSR) & (LSR_TDRQ | LSR_TEMT)) != (LSR_TDRQ | LSR_TEMT))
		;
}

void t31_spl_puts(const char *s)
{
	while (*s)
		t31_spl_putc(*s++);
}
