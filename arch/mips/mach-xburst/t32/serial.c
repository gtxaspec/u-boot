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
 * The GPIO pinmux uses the correct 0x100 port stride (port B =
 * GPIO_BASE + 1 * 0x100) and the exact vendor gpio_set_func() FUNC_0
 * sequence (PXINTC/PXMSKC/PXPAT1C/PXPAT0C), NOT the legacy 0x1000
 * stride - that bug was fatal for the T20 console RX and is avoided
 * here from the start. QEMU does not model pads so this only matters
 * on real silicon; the T32 console UART pin/func should be
 * re-verified against the vendor PRJ gpio table before HW use.
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
 * Mux UART1 (PB23 TX, PB24 RX) to device function 0. XBurst1 GPIO
 * port stride is 0x100; port B = GPIO_BASE + 1 * 0x100 = 0xb0010100.
 * Vendor gpio_set_func() FUNC_0: write the pin mask to PXINTC,
 * PXMSKC, PXPAT1C, PXPAT0C (no pull-register writes).
 */
#define GPIO_PORTB_BASE	(GPIO_BASE + 1 * 0x100)	/* port B, 0x100 stride */
#define G_PXINTC	0x18
#define G_PXMSKC	0x28
#define G_PXPAT1C	0x38
#define G_PXPAT0C	0x48
#define UART1_PINS	(0x3u << 23)	/* PB23 TX + PB24 RX */

static void t32_uart1_pinmux(void)
{
	void __iomem *b = (void __iomem *)GPIO_PORTB_BASE;

	writel(UART1_PINS, b + G_PXINTC);	/* clear INT  */
	writel(UART1_PINS, b + G_PXMSKC);	/* clear MASK -> device fn */
	writel(UART1_PINS, b + G_PXPAT1C);	/* PAT1 = 0 \ func 0     */
	writel(UART1_PINS, b + G_PXPAT0C);	/* PAT0 = 0 /            */
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
