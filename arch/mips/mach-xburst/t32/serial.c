// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal Ingenic T32 SPL UART (UART1 console)
 *
 * Faithful transliteration of the vendor jz_serial driver: the
 * Ingenic UART must be taken out of SIR/IrDA mode (ISR register) and
 * brought up in a specific order, otherwise it produces no output on
 * real silicon. The jz_uart register sequence is XBurst1-generic and
 * identical to T31/T23/T20 (UART1 at 0xb0031000); the SPL runs with
 * no driver model, full U-Boot uses the DM ns16550 driver. The UART
 * is clocked from the 24 MHz EXTAL before pll_init().
 *
 * The GPIO pinmux uses the T31-class 0x1000 port stride (port B =
 * GPIO_BASE + 1 * 0x1000 = 0xb0011000), confirmed by the vendor
 * drivers/gpio/jz_gpio_common.c: CONFIG_PRJ -> JZGPIO_GROUP_OFFSET
 * = 0x1000. UART1 pins are PB23/PB24 funcsel 0 per the vendor
 * PRJ-pinctrl.dtsi uart1_pb node. QEMU does not model pads, so an
 * incorrect 0x100 stride (the T20 layout) boots fine in emulation
 * but produces no UART on real T32 silicon - the pinmux writes hit
 * an unmapped window and the pads stay GPIO.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t32.h>

/* jz_uart byte registers, 4-byte stride */
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

#define T32_UART1_CLK	24000000	/* EXTAL feeds the baud generator */
#define T32_UART1_BAUD	115200

static void u_wb(u8 v, unsigned int off)
{
	writeb(v, (void __iomem *)(UART1_BASE + off));
}

static u8 u_rb(unsigned int off)
{
	return readb((void __iomem *)(UART1_BASE + off));
}

/*
 * Mux UART1 (PB23 TX, PB24 RX) to device function 0. T32 silicon
 * REQUIRES the shadow-register protocol from vendor jz_gpio_common.c
 * gpio_set_func(): PXSHDS=1 enables shadow mode, writes to PXINT/
 * MSK/PAT1/PAT0 *S/*C go to a holding latch, PXUPD=1 commits the
 * latch atomically, PXSHDC=1 releases shadow mode. Direct writes
 * to PXINTC/PXMSKC/PXPAT1C/PXPAT0C without the shadow handshake
 * leave the pad in its reset state (GPIO input) on real T32 - the
 * SPL boots fine but no character ever reaches the UART pad.
 * T31-class GPIO port stride is 0x1000; port B = GPIO_BASE +
 * 1 * 0x1000 = 0xb0011000. Per vendor PRJ-pinctrl.dtsi uart1_pb.
 */
#define GPIO_PORTB_BASE	(GPIO_BASE + 1 * 0x1000)	/* port B, 0x1000 stride */
#define G_PXINTS	0x14
#define G_PXINTC	0x18
#define G_PXMSKS	0x24
#define G_PXMSKC	0x28
#define G_PXPAT1S	0x34
#define G_PXPAT1C	0x38
#define G_PXPAT0S	0x44
#define G_PXPAT0C	0x48
#define G_PXSHDS	0x1d4
#define G_PXSHDC	0x1d8
#define G_PXUPD		0x1e4
#define UART1_PINS	(0x3u << 23)	/* PB23 TX + PB24 RX */

static void t32_uart1_pinmux(void)
{
	void __iomem *b = (void __iomem *)GPIO_PORTB_BASE;

	writel(1, b + G_PXSHDS);		/* enable shadow */
	writel(0, b + G_PXINTS);		/* func 0: INT  = 0 */
	writel(0, b + G_PXMSKS);		/* func 0: MSK  = 0 */
	writel(0, b + G_PXPAT1S);		/* func 0: PAT1 = 0 */
	writel(0, b + G_PXPAT0S);		/* func 0: PAT0 = 0 */
	writel(UART1_PINS, b + G_PXINTC);	/* clear INT bits */
	writel(UART1_PINS, b + G_PXMSKC);	/* clear MSK bits */
	writel(UART1_PINS, b + G_PXPAT1C);	/* clear PAT1 bits */
	writel(UART1_PINS, b + G_PXPAT0C);	/* clear PAT0 bits */
	writel(1, b + G_PXUPD);			/* commit shadow */
	writel(1, b + G_PXSHDC);		/* release shadow */
}

void t32_spl_serial_init(void)
{
	unsigned int div = T32_UART1_CLK / 16 / T32_UART1_BAUD;
	u8 lcr;

	t32_uart1_pinmux();

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

void t32_spl_putc(char c)
{
	if (c == '\n')
		t32_spl_putc('\r');
	u_wb((u8)c, U_THR_DLL);
	while ((u_rb(U_LSR) & (LSR_TDRQ | LSR_TEMT)) != (LSR_TDRQ | LSR_TEMT))
		;
}

void t32_spl_puts(const char *s)
{
	while (*s)
		t32_spl_putc(*s++);
}
