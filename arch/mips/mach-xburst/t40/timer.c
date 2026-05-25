// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T40 (XBurst2) timer - Global OST
 *
 * Same Global OST IP as A1: free-running 64-bit up-counter fed from
 * EXTAL (24 MHz) through a /4 prescale, ticking at 6 MHz. XBurst2 has
 * no usable CP0 Count, so U-Boot's get_ticks() / get_tbclk() come
 * from the Global OST.
 */

#include <config.h>
#include <init.h>
#include <time.h>
#include <linux/compiler.h>
#include <asm/io.h>
#include <mach/t40.h>

#define G_OSTCCR	0x00
#define G_OSTER		0x04
#define G_OSTCR		0x08
#define G_OSTCNTL	0x10
#define G_OSTCNTB	0x14

#define G_OSTCCR_PRESCALE_4	0x1
#define G_OST_RATE		(24000000 / 4)

static u32 gost_readl(u32 off)
{
	return readl((void __iomem *)(G_OST_BASE + off));
}

static void gost_writel(u32 val, u32 off)
{
	writel(val, (void __iomem *)(G_OST_BASE + off));
}

int timer_init(void)
{
	gost_writel(0, G_OSTER);
	gost_writel(G_OSTCCR_PRESCALE_4, G_OSTCCR);
	gost_writel(1, G_OSTCR);
	gost_writel(1, G_OSTER);

	return 0;
}

uint64_t notrace get_ticks(void)
{
	u32 low = gost_readl(G_OSTCNTL);
	u32 high = gost_readl(G_OSTCNTB);

	return ((uint64_t)high << 32) | low;
}

ulong notrace get_tbclk(void)
{
	return G_OST_RATE;
}
