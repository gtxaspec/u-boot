/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T23 SoC register definitions
 *
 * T23 is XBurst1, same peripheral memory map as T31 (verified
 * identical: CPM/UART/SFC/MSC/DDRC/OTG bases and CPM offsets).
 * Kept as a separate header so per-SoC deltas (if any surface)
 * stay local; today it mirrors the T31 map.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __T23_H__
#define __T23_H__

/* APB bus peripherals */
#define CPM_BASE	0xb0000000
#define INTC_BASE	0xb0001000
#define TCU_BASE	0xb0002000
#define WDT_BASE	0xb0002000
#define RTC_BASE	0xb0003000
#define GPIO_BASE	0xb0010000
#define UART0_BASE	0xb0030000
#define UART1_BASE	0xb0031000
#define UART2_BASE	0xb0032000
#define I2C0_BASE	0xb0050000
#define I2C1_BASE	0xb0051000
#define SSI0_BASE	0xb0043000

#define OST_BASE	0xb2000000

/* AHB2 bus peripherals */
#define NEMC_BASE	0xb3410000
#define PDMA_BASE	0xb3420000
#define SFC_BASE	0xb3440000
#define MSC0_BASE	0xb3450000
#define MSC1_BASE	0xb3460000
#define ETHC_BASE	0xb34b0000
#define DDRC_BASE	0xb34f0000
#define OTG_BASE	0xb3500000
#define EFUSE_BASE	0xb3540000

#define DDR_PHY_OFFSET	(-0x4e0000 + 0x1000)

#define UART_BASE	UART0_BASE
#define UART_STEP	0x1000

/* Console UART index (UART1, PB23 TX / PB24 RX - same as T31) */
#define T23_CONSOLE_UART	1

/* CPM (Clock, Reset, Power Controller) register offsets */
#define CPM_CPCCR	0x00
#define CPM_CPAPCR	0x10	/* APLL */
#define CPM_CPMPCR	0x14	/* MPLL */
#define CPM_CLKGR0	0x20
#define CPM_OPCR	0x24
#define CPM_CLKGR1	0x28
#define CPM_DDRCDR	0x2c
#define CPM_USBPCR	0x3c	/* USB PHY control */
#define CPM_USBRDT	0x40	/* USB reset detect timer */
#define CPM_USBVBFIL	0x44	/* USB VBUS jitter filter */
#define CPM_USBPCR1	0x48	/* USB PHY control 1 (ref clock) */
#define CPM_SRBC	0xc4	/* soft reset & bus control */
#define CPM_MSC0CDR	0x68	/* MSC0 clock divider (also MSC src sel) */
#define CPM_MSC1CDR	0xa4	/* MSC1 clock divider */
#define CPM_CPCSR	0xd4
#define CPM_CPVPCR	0xe0	/* VPLL (present but unused on T23) */

/*
 * MSCnCDR layout: [31:30] source select (0 APLL, 1 MPLL, 2 VPLL),
 * [29] CE (apply, left set by the vendor), [28] BUSY, [27] STOP,
 * [7:0] div; rate = src / ((div+1)*2).
 */
#define MSCCDR_CE		BIT(29)
#define MSCCDR_BUSY		BIT(28)
#define MSCCDR_STOP_SHIFT	27
#define MSCCDR_DIV_MASK		0xffu

/* CLKGR0 gate bits */
#define CPM_CLKGR0_DDR		BIT(31)
#define CPM_CLKGR0_TCU		BIT(30)
#define CPM_CLKGR0_SFC		BIT(20)
#define CPM_CLKGR0_UART2	BIT(16)
#define CPM_CLKGR0_UART1	BIT(15)
#define CPM_CLKGR0_UART0	BIT(14)
#define CPM_CLKGR0_MSC1		BIT(5)
#define CPM_CLKGR0_MSC0		BIT(4)
#define CPM_CLKGR0_OTG		BIT(3)

/* CLKGR1 gate bits */
#define CPM_CLKGR1_SYS_OST	BIT(11)
#define CPM_CLKGR1_GMAC		BIT(4)

/* PLL control register layout (CPAPCR/CPMPCR/CPVPCR) */
#define PLL_PLLEN	BIT(0)
#define PLL_PLLON	BIT(3)	/* PLL stable/lock status */

/* GPIO port register offsets, port stride 0x100 (PA..PD) */
#define GPIO_PXPIN(n)	(0x00 + (n) * 0x100)
#define GPIO_PXINT(n)	(0x10 + (n) * 0x100)
#define GPIO_PXINTS(n)	(0x14 + (n) * 0x100)
#define GPIO_PXINTC(n)	(0x18 + (n) * 0x100)
#define GPIO_PXMSK(n)	(0x20 + (n) * 0x100)
#define GPIO_PXMSKS(n)	(0x24 + (n) * 0x100)
#define GPIO_PXMSKC(n)	(0x28 + (n) * 0x100)
#define GPIO_PXPAT1(n)	(0x30 + (n) * 0x100)
#define GPIO_PXPAT1S(n)	(0x34 + (n) * 0x100)
#define GPIO_PXPAT1C(n)	(0x38 + (n) * 0x100)
#define GPIO_PXPAT0(n)	(0x40 + (n) * 0x100)
#define GPIO_PXPAT0S(n)	(0x44 + (n) * 0x100)
#define GPIO_PXPAT0C(n)	(0x48 + (n) * 0x100)

#endif /* __T23_H__ */
