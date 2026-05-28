// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 (XBurst2) timer - Global OST
 *
 * XBurst2 has no usable CP0 Count register, so the generic MIPS
 * read_c0_count() timebase never advances: get_timer() and udelay()
 * spin forever. Drive U-Boot's timebase off the Global OST instead,
 * mirroring the vendor U-Boot gost_timer.c.
 *
 * The Global OST is a free-running 64-bit up-counter fed from EXTAL
 * (24 MHz) through a /4 prescale, so it ticks at 6 MHz. Reading the
 * low half latches the high half into a buffer register, keeping the
 * 64-bit read coherent. timer_init() runs from the board_init_f init
 * sequence; get_ticks()/get_tbclk() override the weak generic ones in
 * lib/time.c, on top of which the generic get_timer()/__udelay() work.
 */

#include <config.h>
#include <init.h>
#include <time.h>
#include <linux/compiler.h>
#include <asm/io.h>
#include <mach/a1.h>

/* Global OST registers, relative to G_OST_BASE (mach/a1.h). */
#define G_OSTCCR	0x00	/* clock control (prescale select) */
#define G_OSTER		0x04	/* counter enable */
#define G_OSTCR		0x08	/* counter clear */
#define G_OSTCNTL	0x10	/* count, low 32 bits */
#define G_OSTCNTB	0x14	/* count, high 32 bits (buffered) */

#define G_OSTCCR_PRESCALE_4	0x1		/* EXTAL / 4 */
#define G_OST_RATE		(24000000 / 4)	/* 6 MHz */

/*
 * Built in SPL too: DM-in-SPL DDR bring-up routes through the generic
 * udelay()/mdelay() (drivers/ram/ingenic uses them), which need a real
 * timebase. XBurst2 has no CP0 Count, so without the Global OST the SPL
 * udelay() spins forever - board_init_f calls timer_init() before the
 * UCLASS_RAM probe.
 */
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
	gost_writel(0, G_OSTER);			/* disable */
	gost_writel(G_OSTCCR_PRESCALE_4, G_OSTCCR);	/* EXTAL / 4 */
	gost_writel(1, G_OSTCR);			/* clear count */
	gost_writel(1, G_OSTER);			/* enable */

	return 0;
}

/*
 * Reading the low half latches the high half into the buffer register,
 * so the 64-bit value is sampled coherently.
 */
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
