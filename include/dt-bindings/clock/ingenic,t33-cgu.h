/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides clock numbers for the ingenic,t33-cgu DT binding.
 *
 * They are roughly ordered as:
 *   - external clocks
 *   - PLLs
 *   - muxes/dividers in the order they appear in the T33 programmers manual
 *   - gates in order of their bit in the CLKGR* registers
 *
 * Modelled on the ingenic,t31-cgu binding (T33 = PRJ008, "T31-extended");
 * a single device tree is valid on both Linux and U-Boot.
 */

#ifndef __DT_BINDINGS_CLOCK_T33_CGU_H__
#define __DT_BINDINGS_CLOCK_T33_CGU_H__

#define T33_CLK_EXCLK		0
#define T33_CLK_RTCLK		1
#define T33_CLK_APLL		2
#define T33_CLK_MPLL		3
#define T33_CLK_VPLL		4
#define T33_CLK_OTGPHY		5
#define T33_CLK_SCLKA		6
#define T33_CLK_CPUMUX		7
#define T33_CLK_CPU		8
#define T33_CLK_L2CACHE		9
#define T33_CLK_AHB0		10
#define T33_CLK_AHB2		11
#define T33_CLK_PCLK		12
#define T33_CLK_DDR		13
#define T33_CLK_MAC		14
#define T33_CLK_LCD		15
#define T33_CLK_MSCMUX		16
#define T33_CLK_MSC0		17
#define T33_CLK_MSC1		18
#define T33_CLK_SFCPLL		19
#define T33_CLK_ISP		20
#define T33_CLK_CIM		21
#define T33_CLK_RSA		22
#define T33_CLK_NEMC		23
#define T33_CLK_EFUSE		24
#define T33_CLK_OTG		25
#define T33_CLK_SSI0		26
#define T33_CLK_SMB0		27
#define T33_CLK_SMB1		28
#define T33_CLK_UART0		29
#define T33_CLK_UART1		30
#define T33_CLK_UART2		31
#define T33_CLK_DMIC		32
#define T33_CLK_AIC		33
#define T33_CLK_SFC		34
#define T33_CLK_SFC0		34
#define T33_CLK_SFC1		35
#define T33_CLK_PDMA		36
#define T33_CLK_TCU		37
#define T33_CLK_DES		38
#define T33_CLK_HASH		39
#define T33_CLK_MIPI_CSI	40
#define T33_CLK_RISCV		41
#define T33_CLK_SADC		42
#define T33_CLK_AHB1		43
#define T33_CLK_AES		44
#define T33_CLK_GMAC		45
#define T33_CLK_IPU		46
#define T33_CLK_DTRNG		47
#define T33_CLK_OST		48
#define T33_CLK_RTC		49
#define T33_CLK_USBPHY		50

#endif /* __DT_BINDINGS_CLOCK_T33_CGU_H__ */
