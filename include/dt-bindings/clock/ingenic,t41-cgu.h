/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides clock numbers for the ingenic,t41-cgu DT binding.
 *
 * IDs match the T40 binding numerically since both SoCs share the same
 * clk-t40.c driver (the driver branches on CONFIG_SOC_T41 for the bits
 * that diverge).
 *
 * Ordered as: external clocks, PLLs, then peripheral leaf clocks.
 */

#ifndef __DT_BINDINGS_CLOCK_T41_CGU_H__
#define __DT_BINDINGS_CLOCK_T41_CGU_H__

#define T41_CLK_EXCLK		0
#define T41_CLK_RTCLK		1
#define T41_CLK_APLL		2
#define T41_CLK_MPLL		3
#define T41_CLK_EPLL		4
#define T41_CLK_VPLL		5
#define T41_CLK_DDR		6
#define T41_CLK_SFC		7
#define T41_CLK_SFC1		8
#define T41_CLK_MSC0		9
#define T41_CLK_MSC1		10
#define T41_CLK_UART0		11
#define T41_CLK_UART1		12
#define T41_CLK_UART2		13
#define T41_CLK_OTG0		14
#define T41_CLK_GMAC0		15
#define T41_CLK_GMAC1		16
#define T41_CLK_TCU		17
#define T41_CLK_OST		18
#define T41_CLK_AIC		19
#define T41_CLK_DMAC		20
#define T41_CLK_EFUSE		21
#define T41_CLK_OTG1		22
#define T41_CLK_OTG2		23

#endif /* __DT_BINDINGS_CLOCK_T41_CGU_H__ */
