// SPDX-License-Identifier: GPL-2.0+
/*
 * T31 system reset.
 *
 * Mainline arch/mips/cpu/cpu.c provides only a weak _machine_restart()
 * that prints "*** reset failed ***" and spins, so `reset` does nothing
 * on T31. Provide a strong override that resets the SoC via the
 * watchdog timer, mirroring the vendor U-Boot sequence: clear the WDT
 * clock-stop bit in the TCU, load a short timeout, clock the WDT from
 * the 32 kHz RTC divided by 64, then enable the counter. The WDT
 * expiry resets the whole SoC (~4 ms later).
 *
 * Built into U-Boot proper only (the SPL never runs `reset`), gated the
 * same way soc.c gates its SPL-only body.
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <mach/t31.h>

#ifndef CONFIG_XPL_BUILD

/* TCU stop-clear register; writing WDTSC starts the WDT clock. */
#define TCU_TSCR		0x3c
#define TSCR_WDTSC		(1 << 16)

/* WDT registers, relative to WDT_BASE. */
#define WDT_TDR			0x0	/* timeout compare value */
#define WDT_TCER		0x4	/* counter enable */
#define WDT_TCNT		0x8	/* counter */
#define WDT_TCSR		0xc	/* clock select / prescale */

#define TCSR_PRESCALE_64	(3 << 3)
#define TCSR_RTC_EN		(1 << 1)
#define TCER_TCEN		(1 << 0)

#define RTC_FREQ		32768
#define WDT_DIV			64
#define RESET_DELAY_MS		4

void _machine_restart(void)
{
	int time = RTC_FREQ / WDT_DIV * RESET_DELAY_MS / 1000;

	if (time > 65535)
		time = 65535;

	writel(TSCR_WDTSC, (void __iomem *)(TCU_BASE + TCU_TSCR));

	writel(0, (void __iomem *)(WDT_BASE + WDT_TCNT));
	writel(time, (void __iomem *)(WDT_BASE + WDT_TDR));
	writel(TCSR_PRESCALE_64 | TCSR_RTC_EN,
	       (void __iomem *)(WDT_BASE + WDT_TCSR));
	writel(0, (void __iomem *)(WDT_BASE + WDT_TCER));

	writel(TCER_TCEN, (void __iomem *)(WDT_BASE + WDT_TCER));

	mdelay(1000);
}

#endif /* !CONFIG_XPL_BUILD */
