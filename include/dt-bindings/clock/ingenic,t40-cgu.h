/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides clock numbers for the ingenic,t40-cgu DT binding.
 *
 * Ordered as: external clocks, PLLs, then peripheral leaf clocks.
 */

#ifndef __DT_BINDINGS_CLOCK_T40_CGU_H__
#define __DT_BINDINGS_CLOCK_T40_CGU_H__

#define T40_CLK_EXCLK		0
#define T40_CLK_RTCLK		1
#define T40_CLK_APLL		2
#define T40_CLK_MPLL		3
#define T40_CLK_EPLL		4
#define T40_CLK_VPLL		5
#define T40_CLK_DDR		6
#define T40_CLK_SFC		7
#define T40_CLK_SFC1		8
#define T40_CLK_MSC0		9
#define T40_CLK_MSC1		10
#define T40_CLK_UART0		11
#define T40_CLK_UART1		12
#define T40_CLK_UART2		13
#define T40_CLK_OTG0		14
#define T40_CLK_GMAC0		15
#define T40_CLK_GMAC1		16
#define T40_CLK_TCU		17
#define T40_CLK_OST		18
#define T40_CLK_AIC		19
#define T40_CLK_DMAC		20
#define T40_CLK_EFUSE		21
#define T40_CLK_OTG1		22
#define T40_CLK_OTG2		23

#endif /* __DT_BINDINGS_CLOCK_T40_CGU_H__ */
