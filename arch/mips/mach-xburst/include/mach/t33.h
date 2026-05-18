/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T33 SoC register definitions
 *
 * T33 is the vendor "PRJ008" - XBurst1, "T31-extended" (same
 * peripheral memory map and CPM offsets as T31: CPM/UART/WDT/DDRC
 * bases and CPM register offsets identical, verified vs vendor
 * arch-PRJ). Forward-ported from the modern U-Boot 2022.10 vendor
 * branch (T33-2.0.2.1-uboot2022.10), not the legacy T31-1.1.6
 * bare-metal tree. The PLL uses the M/N/OD0/OD1 form (CPAPCR/
 * CPMPCR/CPVPCR), like T31/T23/T20.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __T33_H__
#define __T33_H__

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

#define OST_BASE	0xb2000000

/* AHB2 bus peripherals */
#define NEMC_BASE	0xb3410000
#define PDMA_BASE	0xb3420000
#define SFC_BASE	0xb3440000
#define MSC0_BASE	0xb3450000
#define MSC1_BASE	0xb3460000
#define DDRC_BASE	0xb34f0000
#define OTG_BASE	0xb3500000
#define EFUSE_BASE	0xb3540000

#define UART_BASE	UART0_BASE
#define UART_STEP	0x1000

/* Console UART index (UART1, T33 default; CONFIG_SYS_UART_INDEX=1) */
#define T33_CONSOLE_UART	1

/*
 * SoC identification register (vendor SOCID_ADDRESS). T33 = PRJ008.
 * The SPL reads it to confirm it is running on T33 silicon.
 */
#define T33_SOCID_ADDR		0xb300002c
#define T33_SOCID		0x10033004	/* PRJ008 */
#define T32_SOCID		0x10032004	/* PRJ007 */

/* CPM (Clock, Reset, Power Controller) register offsets */
#define CPM_CPCCR	0x00
#define CPM_CPAPCR	0x10	/* APLL */
#define CPM_CPMPCR	0x14	/* MPLL */
#define CPM_CLKGR0	0x20
#define CPM_OPCR	0x24
#define CPM_CLKGR1	0x28
#define CPM_DDRCDR	0x2c
#define CPM_SRBC0	0xc4	/* soft reset/bus control (DDRC/DDRP) */
#define CPM_CPCSR	0xd4
#define CPM_MESTSEL	0xec
#define CPM_CPVPCR	0xe0	/* VPLL (unused on T33/PRJ008) */

/* CLKGR0 gate bits */
#define CPM_CLKGR0_UART2	BIT(16)
#define CPM_CLKGR0_UART1	BIT(15)
#define CPM_CLKGR0_UART0	BIT(14)

/* OST gate lives in CLKGR1 bit 7 (vendor CPM_CLKGR1_OST) */
#define CPM_CLKGR1_OST		BIT(7)

/* Watchdog register offsets (vendor arch-PRJ/wdt.h) */
#define WDT_TDR			0x0
#define WDT_TCER		0x4
#define WDT_TCNT		0x8
#define WDT_TCSR		0xc

/* PLL control register layout (CPAPCR/CPMPCR/CPVPCR) */
#define PLL_PLLEN	BIT(0)
#define PLL_PLLON	BIT(3)	/* PLL stable/lock status */

/* GPIO port register offsets, port stride 0x100 (PA..PD) */
#define GPIO_PXPIN(n)	(0x00 + (n) * 0x100)
#define GPIO_PXINTC(n)	(0x18 + (n) * 0x100)
#define GPIO_PXMSKC(n)	(0x28 + (n) * 0x100)
#define GPIO_PXPAT1C(n)	(0x38 + (n) * 0x100)
#define GPIO_PXPAT0C(n)	(0x48 + (n) * 0x100)

#endif /* __T33_H__ */
