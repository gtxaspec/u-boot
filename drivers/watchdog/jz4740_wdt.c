// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst (jz4740-style) watchdog timer.
 *
 * DM UCLASS_WDT driver. Wired to the U-Boot sysreset framework via
 * the generic watchdog-reboot driver (CONFIG_SYSRESET_WATCHDOG), so
 * the `reset` command and reset_cpu() reset the SoC through DM - no
 * weak _machine_restart() override needed.
 *
 * The watchdog lives in the TCU block (the DT node is a child of the
 * "ingenic,t31-tcu" timer, mirroring the mainline Linux binding so a
 * single device tree is valid on both). It is clocked from the 32 kHz
 * RTC divided by 64; expiry resets the whole SoC. The compatible
 * strings match the mainline Linux port (ingenic,t31-watchdog /
 * ingenic,jz4780-watchdog).
 */

#include <dm.h>
#include <wdt.h>
#include <asm/io.h>
#include <linux/bitops.h>

/*
 * The TCU/WDT register block is at physical 0x10002000; access it via
 * the uncached MIPS KSEG1 window, like the other proven T31 U-Boot
 * drivers. TSCR (the WDT clock stop-clear register) lives at +0x3c,
 * outside the watchdog child's nominal reg window but in the same
 * block, so use the fixed block base.
 */
#define TCU_BASE		0xb0002000

#define TCU_TSCR		0x3c		/* timer stop-clear */
#define TSCR_WDTSC		BIT(16)		/* start the WDT clock */

#define WDT_TDR			0x00		/* timeout compare */
#define WDT_TCER		0x04		/* counter enable */
#define WDT_TCNT		0x08		/* counter */
#define WDT_TCSR		0x0c		/* clock select */

#define TCSR_PRESCALE_64	(3 << 3)
#define TCSR_RTC_EN		BIT(1)
#define TCER_TCEN		BIT(0)

#define RTC_FREQ		32768
#define WDT_DIV			64

struct jz4740_wdt_priv {
	void __iomem *base;
};

static u16 jz4740_wdt_ticks(u64 timeout_ms)
{
	u64 t = (u64)(RTC_FREQ / WDT_DIV) * timeout_ms / 1000;

	if (t < 1)
		t = 1;
	if (t > 0xffff)
		t = 0xffff;
	return (u16)t;
}

static int jz4740_wdt_start(struct udevice *dev, u64 timeout_ms,
			    ulong flags)
{
	struct jz4740_wdt_priv *priv = dev_get_priv(dev);

	writel(TSCR_WDTSC, priv->base + TCU_TSCR);

	writel(0, priv->base + WDT_TCNT);
	writel(jz4740_wdt_ticks(timeout_ms), priv->base + WDT_TDR);
	writel(TCSR_PRESCALE_64 | TCSR_RTC_EN, priv->base + WDT_TCSR);
	writel(0, priv->base + WDT_TCER);

	writel(TCER_TCEN, priv->base + WDT_TCER);
	return 0;
}

static int jz4740_wdt_stop(struct udevice *dev)
{
	struct jz4740_wdt_priv *priv = dev_get_priv(dev);

	writel(0, priv->base + WDT_TCER);
	return 0;
}

static int jz4740_wdt_reset(struct udevice *dev)
{
	struct jz4740_wdt_priv *priv = dev_get_priv(dev);

	writel(0, priv->base + WDT_TCNT);
	return 0;
}

static int jz4740_wdt_expire_now(struct udevice *dev, ulong flags)
{
	/* Shortest timeout, then spin until the SoC resets. */
	jz4740_wdt_start(dev, 4, flags);

	while (1)
		;

	return 0;
}

static const struct wdt_ops jz4740_wdt_ops = {
	.start		= jz4740_wdt_start,
	.stop		= jz4740_wdt_stop,
	.reset		= jz4740_wdt_reset,
	.expire_now	= jz4740_wdt_expire_now,
};

static int jz4740_wdt_probe(struct udevice *dev)
{
	struct jz4740_wdt_priv *priv = dev_get_priv(dev);

	priv->base = (void __iomem *)TCU_BASE;
	return 0;
}

static const struct udevice_id jz4740_wdt_ids[] = {
	{ .compatible = "ingenic,t31-watchdog" },
	{ .compatible = "ingenic,t40-watchdog" },
	{ .compatible = "ingenic,jz4780-watchdog" },
	{ .compatible = "ingenic,jz4740-watchdog" },
	{ }
};

U_BOOT_DRIVER(jz4740_wdt) = {
	.name		= "jz4740_wdt",
	.id		= UCLASS_WDT,
	.of_match	= jz4740_wdt_ids,
	.probe		= jz4740_wdt_probe,
	.priv_auto	= sizeof(struct jz4740_wdt_priv),
	.ops		= &jz4740_wdt_ops,
};
