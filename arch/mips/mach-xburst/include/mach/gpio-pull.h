/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Drop the pull-up and pull-down on pins that are not yet under the pinctrl
 * driver (early UART, board and MAC pin setup). The register layout differs:
 * T21/T30 hold two bits per pin at 0x110/0x120 (see pinctrl-ingenic.c), the
 * T23/T31 and later one bit per pin, so a 1-bit clear mask written to a
 * 2-bit part wipes the wrong pins' fields.
 */
#ifndef __XBURST_GPIO_PULL_H
#define __XBURST_GPIO_PULL_H

#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/types.h>

#define XBURST_GPIO_PXPUENC	0x118	/* split: pull-up off; field: PXPELC */
#define XBURST_GPIO_PXPDENC	0x128	/* split: pull-down off; field: PXPEHC */

/* 1-bit-per-pin parts: T23, T31, T32/T33, T40/T41, A1. */
static inline void xburst_gpio_pull_off_split(void __iomem *bank, u32 pins)
{
	writel(pins, bank + XBURST_GPIO_PXPUENC);
	writel(pins, bank + XBURST_GPIO_PXPDENC);
}

/* 2-bit-per-pin parts: T21, T30. Pins 0-15 in PXPEL, 16-31 in PXPEH. */
static inline u32 xburst_gpio_pull_fields(u32 pins)
{
	u32 f = 0;
	int i;

	for (i = 0; i < 16; i++)
		if (pins & BIT(i))
			f |= 3u << (2 * i);
	return f;
}

static inline void xburst_gpio_pull_off_field(void __iomem *bank, u32 pins)
{
	if (pins & 0xffff)
		writel(xburst_gpio_pull_fields(pins & 0xffff),
		       bank + XBURST_GPIO_PXPUENC);
	if (pins >> 16)
		writel(xburst_gpio_pull_fields(pins >> 16),
		       bank + XBURST_GPIO_PXPDENC);
}

#endif
