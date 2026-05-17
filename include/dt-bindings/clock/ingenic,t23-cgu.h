/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides clock numbers for the ingenic,t23-cgu DT binding.
 *
 * T23 is XBurst1 like T31 and shares the CGU register model; the ID
 * layout mirrors ingenic,t31-cgu so a device tree is portable. T23
 * has no VPLL (the ID is kept reserved for binding stability; the
 * driver does not implement it).
 *
 *   - external clocks
 *   - PLLs
 *   - muxes/dividers in the order they appear in the T23 programmers manual
 *   - gates in order of their bit in the CLKGR* registers
 */

#ifndef __DT_BINDINGS_CLOCK_T23_CGU_H__
#define __DT_BINDINGS_CLOCK_T23_CGU_H__

#define T23_CLK_EXCLK		0
#define T23_CLK_RTCLK		1
#define T23_CLK_APLL		2
#define T23_CLK_MPLL		3
#define T23_CLK_VPLL		4	/* reserved: T23 has no VPLL */
#define T23_CLK_OTGPHY		5
#define T23_CLK_SCLKA		6
#define T23_CLK_CPUMUX		7
#define T23_CLK_CPU		8
#define T23_CLK_L2CACHE		9
#define T23_CLK_AHB0		10
#define T23_CLK_AHB2		11
#define T23_CLK_PCLK		12
#define T23_CLK_DDR		13
#define T23_CLK_MAC		14
#define T23_CLK_LCD		15
#define T23_CLK_MSCMUX		16
#define T23_CLK_MSC0		17
#define T23_CLK_MSC1		18
#define T23_CLK_SSIPLL		19
#define T23_CLK_ISP		20
#define T23_CLK_CIM		21
#define T23_CLK_RSA		22
#define T23_CLK_EL150		23
#define T23_CLK_NEMC		24
#define T23_CLK_EFUSE		25
#define T23_CLK_OTG		26
#define T23_CLK_SSI0		27
#define T23_CLK_SSI1		28
#define T23_CLK_SMB0		29
#define T23_CLK_SMB1		30
#define T23_CLK_UART0		31
#define T23_CLK_UART1		32
#define T23_CLK_UART2		33
#define T23_CLK_DMIC		34
#define T23_CLK_AIC		35
#define T23_CLK_SFC		36
#define T23_CLK_PDMA		37
#define T23_CLK_TCU		38
#define T23_CLK_DES		39
#define T23_CLK_HASH		40
#define T23_CLK_MIPI_CSI	41
#define T23_CLK_RISCV		42
#define T23_CLK_SADC		43
#define T23_CLK_SLV		44
#define T23_CLK_AHB1		45
#define T23_CLK_AES		46
#define T23_CLK_GMAC		47
#define T23_CLK_IPU		48
#define T23_CLK_DTRNG		49
#define T23_CLK_OST		50
#define T23_CLK_EXCLK_DIV512	51
#define T23_CLK_RTC		52
#define T23_CLK_USBPHY		53
#define T23_CLK_DIV_I2ST	54
#define T23_CLK_DIV_I2SR	55
#define T23_CLK_CE_I2ST		56
#define T23_CLK_CE_I2SR		57

#endif /* __DT_BINDINGS_CLOCK_T23_CGU_H__ */
