/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic A1 SoC register definitions (XBurst2)
 *
 * Copyright (c) 2020 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __A1_H__
#define __A1_H__

/* APB bus peripherals */
#define CPM_BASE	0xb0000000
#define TCU_BASE	0xb0002000
#define WDT_BASE	0xb0002000
#define RTC_BASE	0xb0003000
#define GPIO_BASE	0xb0010000
#define AIC0_BASE	0xb0020000
#define AIC1_BASE	0xb0021000
#define CODEC_BASE	0xb0022000
#define UART0_BASE	0xb0030000
#define UART1_BASE	0xb0031000
#define UART2_BASE	0xb0032000
#define I2C0_BASE	0xb0050000
#define I2C1_BASE	0xb0051000
#define USB_PHY_BASE	0xb0060000

/* AHB0 bus peripherals */
#define G_OST_BASE	0xb2000000
#define N_OST_BASE	0xb2100000
#define CCU_BASE	0xb2200000
#define INTCN_BASE	0xb2300000

/* AHB bus peripherals */
#define MSC0_BASE	0xb3060000
#define MSC1_BASE	0xb3070000
#define GMAC0_BASE	0xb30b0000
#define GMAC1_BASE	0xb30c0000
#define DDR_PHY_BASE	0xb3011000
#define NEMC_BASE	0xb3410000
#define PDMA_BASE	0xb3420000
#define SFC_BASE	0xb3440000
#define SFC1_BASE	0xb3450000
#define DDRC_BASE	0xb34f0000
#define OTG0_BASE	0xb3600000
#define OTG1_BASE	0xb3640000
#define OTG2_BASE	0xb3680000
#define EFUSE_BASE	0xb3540000

#define DDR_PHY_OFFSET	(-0x4e0000 + 0x1000)

#define UART_BASE	UART0_BASE
#define UART_STEP	0x1000

/* Console UART index (UART1 -> ttyS1) */
#define A1_CONSOLE_UART	1

/* CPM register offsets */
#define CPM_CPCCR	0x00
#define CPM_LCR		0x04
#define CPM_RSR		0x08
#define CPM_CPPCR	0x0c
#define CPM_CPAPCR	0x10	/* APLL */
#define CPM_CPMPCR	0x14	/* MPLL */
#define CPM_CPEPCR	0x18	/* EPLL */
#define CPM_CPVPCR	0x1c	/* VPLL */
#define CPM_CLKGR0	0x30
#define CPM_OPCR	0x34
#define CPM_CLKGR1	0x38
#define CPM_DDRCDR	0x3c
#define CPM_CPSPR	0x44
#define CPM_CPSPPR	0x48
#define CPM_AHB1CDR	0x84
#define CPM_SFC0CDR	0x90
#define CPM_MSC0CDR	0x98
#define CPM_CPCSR	0xec
#define CPM_SRBC0	0xf0

/* CLKGR0 gate bits */
#define CPM_CLKGR0_CPU		BIT(0)
#define CPM_CLKGR0_APB0	BIT(1)
#define CPM_CLKGR0_AHB0	BIT(2)
#define CPM_CLKGR0_DDR		BIT(3)
#define CPM_CLKGR0_EFUSE	BIT(4)
#define CPM_CLKGR0_TCU		BIT(5)
#define CPM_CLKGR0_OST		BIT(6)
#define CPM_CLKGR0_NEMC	BIT(7)
#define CPM_CLKGR0_UART0	BIT(8)
#define CPM_CLKGR0_UART1	BIT(9)
#define CPM_CLKGR0_UART2	BIT(10)
#define CPM_CLKGR0_OTG0	BIT(11)
#define CPM_CLKGR0_OTG1	BIT(12)
#define CPM_CLKGR0_OTG2	BIT(13)
#define CPM_CLKGR0_MSC0	BIT(14)
#define CPM_CLKGR0_MSC1	BIT(15)
#define CPM_CLKGR0_SFC0	BIT(24)
#define CPM_CLKGR0_SFC1	BIT(25)
#define CPM_CLKGR0_AIC		BIT(30)

/* CLKGR1 gate bits */
#define CPM_CLKGR1_DMAC	BIT(3)
#define CPM_CLKGR1_GMAC0	BIT(8)
#define CPM_CLKGR1_GMAC1	BIT(10)

/* USB OTG PHY control (CPM); three identical OTG cores - OTG0/1/2 */
#define CPM_USB0PCR	0x50	/* USB0 PHY control */
#define CPM_USB0RDT	0x54	/* USB0 reset-detect timer */
#define CPM_USB0VBFIL	0x58	/* USB0 VBUS jitter filter */
#define CPM_USB0PCR1	0x5c	/* USB0 PHY control 1 */
#define CPM_USB1PCR	0x60
#define CPM_USB1RDT	0x64
#define CPM_USB1VBFIL	0x68
#define CPM_USB1PCR1	0x6c
#define CPM_USB2PCR	0x70
#define CPM_USB2RDT	0x74
#define CPM_USB2VBFIL	0x78
#define CPM_USB2PCR1	0x7c

/* CPM_USBnPCR bits (identical layout for all three instances) */
#define USBPCR_USB_MODE		BIT(31)
#define USBPCR_IDPULLUP_MASK	(0x3u << 28)
#define USBPCR_COMMONONN	BIT(25)
#define USBPCR_VBUSVLDEXT	BIT(24)
#define USBPCR_VBUSVLDEXTSEL	BIT(23)
#define USBPCR_POR		BIT(22)
#define USBPCR_SIDDQ		BIT(21)
#define USBPCR_OTG_DISABLE	BIT(20)

/* CPM_USBnPCR1 bits */
#define USBPCR1_IDPULLUP_ZEAR	BIT(30)
#define USBPCR1_DPPULLDOWN	BIT(29)
#define USBPCR1_DMPULLDOWN	BIT(28)
#define USBPCR1_WORD_IF0	BIT(19)

/* CPM_USBnRDT bits */
#define USBRDT_UTMI_RST		BIT(27)
#define USBRDT_VBFIL_LD_EN	BIT(25)

/* CPM_SRBC0 USB core soft-reset bits */
#define SRBC_USB0_SR		BIT(17)
#define SRBC_USB1_SR		BIT(12)
#define SRBC_USB2_SR		BIT(16)

/* CPM_OPCR USB PHY suspend-disable (SPENDN) bits */
#define OPCR_SPENDN0		BIT(7)
#define OPCR_SPENDN1		BIT(6)
#define OPCR_SPENDN2		BIT(5)

/* PLL control register layout (CPAPCR/CPMPCR/CPEPCR/CPVPCR) */
#define PLL_PLLEN	BIT(0)
#define PLL_PLLON	BIT(3)

/* GPIO port register offsets, port stride 0x1000 (PA..PD) */
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

#endif /* __A1_H__ */
