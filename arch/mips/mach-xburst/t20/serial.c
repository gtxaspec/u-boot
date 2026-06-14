// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal Ingenic T20 SPL UART (UART1 console)
 *
 * Faithful transliteration of the vendor jz_serial driver; the
 * sequence is XBurst1-generic and the T20 console is UART1 on
 * PB23 TX / PB24 RX, same as T31/T23 (CONFIG_SYS_UART_INDEX=1;
 * controller step 0x1000). The UART is clocked from the 24 MHz
 * EXTAL before pll_init(); full U-Boot uses the DM ns16550 driver.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t20.h>

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

#define T20_UART1_CLK	24000000	/* EXTAL feeds the baud generator */
#define T20_UART1_BAUD	115200

static void u_wb(u8 v, unsigned int off)
{
	writeb(v, (void __iomem *)(UART1_BASE + off));
}

static u8 u_rb(unsigned int off)
{
	return readb((void __iomem *)(UART1_BASE + off));
}

/*
 * Mux UART1 (PB23 TX, PB24 RX) to device function 0.
 *
 * The first-gen XBurst1 (T10/T20) GPIO bank stride is 0x100, NOT the 0x1000
 * of T21+ (see drivers/pinctrl/pinctrl-ingenic.c BANK_STRIDE_LEGACY): port B
 * is at GPIO_BASE + 1 * 0x100. Vendor gpio_set_func() FUNC_0 = clear INT,
 * MASK, PAT1, PAT0 (UART1 = function 0 on PB23/PB24, per the shared T10-T33
 * group table). Required for NOR cold boot: the mask ROM's USB-boot path
 * happens to leave UART1 TX routed (so the 0x1000 no-op "worked" on USB), but
 * the NOR-boot path does not - without this the SPL runs but is silent.
 */
#define GPIO_PORTB_BASE	(GPIO_BASE + 1 * 0x100)	/* port B, 0x100 stride */
#define G_PXINTC	0x18
#define G_PXMSKC	0x28
#define G_PXPAT1C	0x38
#define G_PXPAT0C	0x48
#define UART1_PINS	(0x3u << 23)	/* PB23 TX + PB24 RX */

static void t20_uart1_pinmux(void)
{
	void __iomem *b = (void __iomem *)GPIO_PORTB_BASE;

	writel(UART1_PINS, b + G_PXINTC);	/* clear INT  */
	writel(UART1_PINS, b + G_PXMSKC);	/* MASK=0 -> device function */
	writel(UART1_PINS, b + G_PXPAT1C);	/* PAT1=0 \ function 0 */
	writel(UART1_PINS, b + G_PXPAT0C);	/* PAT0=0 / */
}

void t20_spl_serial_init(void)
{
	unsigned int div = T20_UART1_CLK / 16 / T20_UART1_BAUD;
	u8 lcr;

	t20_uart1_pinmux();

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

void t20_spl_putc(char c)
{
	if (c == '\n')
		t20_spl_putc('\r');
	u_wb((u8)c, U_THR_DLL);
	while ((u_rb(U_LSR) & (LSR_TDRQ | LSR_TEMT)) != (LSR_TDRQ | LSR_TEMT))
		;
}

void t20_spl_puts(const char *s)
{
	while (*s)
		t20_spl_putc(*s++);
}
