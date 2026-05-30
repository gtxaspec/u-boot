// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal Ingenic T41 SPL UART (UART1 console, PB23 TX / PB24 RX)
 *
 * Same Ingenic UART IP as XBurst1 T-series and A1. Console on UART1
 * at 115200 8N1, clocked from 24 MHz EXTAL before pll_init(). UART1
 * pinmux on T40 = PB23/PB24 function 0 (from the vendor jz_gpio
 * uart_gpio_func[] table - same routing as T31/T32/T33).
 */

#include <asm/io.h>
#include <mach/t41.h>

#define U_THR_DLL	0x00
#define U_IER_DLH	0x04
#define U_FCR		0x08
#define U_LCR		0x0c
#define U_LSR		0x14
#define U_ISR		0x20

#define FCR_FE		(1 << 0)
#define FCR_RFLS	(1 << 1)
#define FCR_TFLS	(1 << 2)
#define FCR_UUE		(1 << 4)
#define LCR_WLEN_8	(3 << 0)
#define LCR_STOP_1	(0 << 2)
#define LCR_DLAB	(1 << 7)
#define LSR_TDRQ	(1 << 5)
#define LSR_TEMT	(1 << 6)
#define SIRCR_TSIRE	(1 << 0)
#define SIRCR_RSIRE	(1 << 1)

#define T41_UART1_CLK	24000000
#define T41_UART1_BAUD	115200

static void u_wb(u8 v, unsigned int off)
{
	writeb(v, (void __iomem *)(UART1_BASE + off));
}

static u8 u_rb(unsigned int off)
{
	return readb((void __iomem *)(UART1_BASE + off));
}

/* T40 UART1: PB23 TX / PB24 RX, function 0. Port B stride 0x1000. */
#define GPIO_PORTB_BASE	0xb0011000
#define G_PXINTC	0x18
#define G_PXMSKC	0x28
#define G_PXPAT1C	0x38
#define G_PXPAT0C	0x48
#define G_PXPUENC	0x118
#define G_PXPDENC	0x128
#define UART1_PINS	(0x3u << 23)

static void t41_uart1_pinmux(void)
{
	void __iomem *b = (void __iomem *)GPIO_PORTB_BASE;

	writel(UART1_PINS, b + G_PXINTC);
	writel(UART1_PINS, b + G_PXMSKC);
	writel(UART1_PINS, b + G_PXPAT1C);
	writel(UART1_PINS, b + G_PXPAT0C);
	writel(UART1_PINS, b + G_PXPUENC);
	writel(UART1_PINS, b + G_PXPDENC);
}

void t41_spl_serial_init(void)
{
	unsigned int div = T41_UART1_CLK / 16 / T41_UART1_BAUD;
	u8 lcr;

	t41_uart1_pinmux();

	/*
	 * Vendor jz_serial_init order writes FCR=~UUE (0xef) and
	 * ISR=~SIR (0xfc) before LCR, but on T40NN those writes appear
	 * to wedge the UART. Skipping them and going straight to LCR /
	 * baud / FCR-enable gives a clean banner once the bootrom-
	 * INGE-programmed PLLs settle. See project_uboot_t40_port.md.
	 */
	u_wb(0, U_IER_DLH);
	u_wb(LCR_WLEN_8 | LCR_STOP_1, U_LCR);

	lcr = u_rb(U_LCR) | LCR_DLAB;
	u_wb(lcr, U_LCR);
	u_wb((div >> 8) & 0xff, U_IER_DLH);
	u_wb(div & 0xff, U_THR_DLL);
	lcr &= ~LCR_DLAB;
	u_wb(lcr, U_LCR);

	u_wb(FCR_UUE | FCR_FE | FCR_TFLS | FCR_RFLS, U_FCR);
}
