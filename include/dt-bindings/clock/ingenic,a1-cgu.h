/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides clock numbers for the ingenic,a1-cgu DT binding.
 *
 * Ordered as: external clocks, PLLs, then peripheral leaf clocks.
 */

#ifndef __DT_BINDINGS_CLOCK_A1_CGU_H__
#define __DT_BINDINGS_CLOCK_A1_CGU_H__

#define A1_CLK_EXCLK		0
#define A1_CLK_RTCLK		1
#define A1_CLK_APLL		2
#define A1_CLK_MPLL		3
#define A1_CLK_EPLL		4
#define A1_CLK_VPLL		5
#define A1_CLK_DDR		6
#define A1_CLK_SFC		7
#define A1_CLK_SFC1		8
#define A1_CLK_MSC0		9
#define A1_CLK_MSC1		10
#define A1_CLK_UART0		11
#define A1_CLK_UART1		12
#define A1_CLK_UART2		13
#define A1_CLK_OTG		14
#define A1_CLK_GMAC0		15
#define A1_CLK_GMAC1		16
#define A1_CLK_TCU		17
#define A1_CLK_OST		18
#define A1_CLK_AIC		19
#define A1_CLK_DMAC		20
#define A1_CLK_EFUSE		21

#endif /* __DT_BINDINGS_CLOCK_A1_CGU_H__ */
