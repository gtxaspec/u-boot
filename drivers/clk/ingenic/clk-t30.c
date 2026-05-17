// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T30 CGU (Clock/Power Manager) clock driver.
 *
 * Runs in U-Boot proper and models the leaf peripheral clocks the
 * U-Boot drivers consume (SFC, MMC, GMAC): recalculates the PLL
 * rates from the CPM registers and provides get/set_rate/enable/
 * disable.
 *
 * T30 shares the XBurst1 CGU leaf-clock CDR/gate map with T31/T23
 * (same SSICDR/MSC0CDR/MACCDR offsets and CLKGR bits) and, like
 * T31, has a VPLL. The one real delta is the PLL register encoding:
 * T30 uses cpm_cpxpcr_t (M[28:20] N[19:14] OD[13:11] RG[7:5]) and
 * rate = 2 * EXT * (M+1) / ((N+1) * 2^OD), unlike the T31/T23
 * M/N/OD1/OD0 form.
 */

#include <clk-uclass.h>
#include <dm.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <dt-bindings/clock/ingenic,t30-cgu.h>

#define T30_CLK_COUNT		(T30_CLK_CE_I2SR + 1)

/* CPM via the uncached MIPS KSEG1 window (as the other proven drivers). */
#define T30_CPM_BASE		0xb0000000

#define CPM_CPCCR		0x00
#define CPM_CPAPCR		0x10	/* APLL */
#define CPM_CPMPCR		0x14	/* MPLL */
#define CPM_CLKGR0		0x20
#define CPM_CLKGR1		0x28
#define CPM_MACCDR		0x54
#define CPM_MSC0CDR		0x68
#define CPM_SSICDR		0x74
#define CPM_MSC1CDR		0xa4
#define CPM_CPVPCR		0xe0	/* VPLL */

#define EXT_RATE		24000000UL
#define RTC_RATE		32768UL

/* CDR source field [31:30]: 0=sclka(~apll) 1=mpll 2=vpll */
#define CDR_SRC_SHIFT		30
#define CDR_SRC_MASK		(3u << CDR_SRC_SHIFT)
#define CDR_SRC_MPLL		(1u << CDR_SRC_SHIFT)
#define CDR_DIV_MASK		0xffu

struct t30_clk_desc {
	u16 cdr;	/* CPM CDR register offset, 0 = no divider */
	u8 ce;		/* clock-change-enable bit in cdr */
	u8 busy;	/* divider-busy bit in cdr */
	u8 stop;	/* clock-stop bit in cdr */
	u16 gate_reg;	/* CLKGR0/CLKGR1 offset, 0xffff = no gate */
	u8 gate_bit;	/* gate bit (set = clock disabled) */
};

#define NO_GATE 0xffff

/*
 * Leaf clocks. Identical CDR/gate map to T31/T23 (same XBurst1 CGU
 * IP): SFC folds the SSIPLL divider + SFC gate, GMAC folds the
 * MAC-PHY divider + GMAC gate.
 */
static const struct t30_clk_desc t30_clks[T30_CLK_COUNT] = {
	[T30_CLK_SFC]  = { CPM_SSICDR, 28, 27, 26, CPM_CLKGR0, 20 },
	[T30_CLK_MSC0] = { CPM_MSC0CDR, 29, 28, 27, CPM_CLKGR0, 4 },
	[T30_CLK_MSC1] = { CPM_MSC1CDR, 29, 28, 27, CPM_CLKGR0, 5 },
	[T30_CLK_GMAC] = { CPM_MACCDR, 29, 28, 27, CPM_CLKGR1, 4 },
	[T30_CLK_UART1] = { 0, 0, 0, 0, CPM_CLKGR0, 15 },
	[T30_CLK_OTG]  = { 0, 0, 0, 0, CPM_CLKGR0, 3 },
	[T30_CLK_TCU]  = { 0, 0, 0, 0, CPM_CLKGR0, 30 },
	[T30_CLK_OST]  = { 0, 0, 0, 0, CPM_CLKGR1, 11 },
};

struct t30_cgu_priv {
	void __iomem *base;
};

static u32 cpm_r(struct t30_cgu_priv *p, u32 off)
{
	return readl(p->base + off);
}

static void cpm_w(struct t30_cgu_priv *p, u32 off, u32 v)
{
	writel(v, p->base + off);
}

/*
 * T30 cpm_cpxpcr_t: PLLM[28:20] PLLN[19:14] PLLOD[13:11] PLLRG[7:5],
 * fbdiv = M+1, refdiv = N+1, fdivq = (OD ? 1<<OD : 1),
 * pllout = 2 * EXT * fbdiv / (refdiv * fdivq).
 */
static ulong pll_rate(struct t30_cgu_priv *p, u32 reg)
{
	u32 v = cpm_r(p, reg);
	u32 m = (v >> 20) & 0x1ff;
	u32 n = (v >> 14) & 0x3f;
	u32 od = (v >> 11) & 0x7;
	u64 rate = (u64)EXT_RATE * 2 * (m + 1);
	u32 fdivq = od ? (1u << od) : 1u;

	return (ulong)(rate / ((n + 1) * fdivq));
}

static ulong t30_parent_rate(struct t30_cgu_priv *p, u32 cdr)
{
	switch ((cpm_r(p, cdr) & CDR_SRC_MASK) >> CDR_SRC_SHIFT) {
	case 1:
		return pll_rate(p, CPM_CPMPCR);	/* MPLL */
	case 2:
		return pll_rate(p, CPM_CPVPCR);	/* VPLL */
	default:
		return pll_rate(p, CPM_CPAPCR);	/* sclka ~ APLL */
	}
}

static ulong t30_clk_get_rate(struct clk *clk)
{
	struct t30_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t30_clk_desc *d;

	switch (clk->id) {
	case T30_CLK_EXCLK:
		return EXT_RATE;
	case T30_CLK_RTCLK:
		return RTC_RATE;
	case T30_CLK_APLL:
		return pll_rate(p, CPM_CPAPCR);
	case T30_CLK_MPLL:
		return pll_rate(p, CPM_CPMPCR);
	case T30_CLK_VPLL:
		return pll_rate(p, CPM_CPVPCR);
	case T30_CLK_UART1:
		return EXT_RATE;	/* T30 UART is clocked from EXT */
	}

	if (clk->id >= T30_CLK_COUNT)
		return -EINVAL;

	d = &t30_clks[clk->id];
	if (!d->cdr)
		return EXT_RATE;

	return t30_parent_rate(p, d->cdr) /
	       ((cpm_r(p, d->cdr) & CDR_DIV_MASK) + 1);
}

static ulong t30_clk_set_rate(struct clk *clk, ulong rate)
{
	struct t30_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t30_clk_desc *d;
	ulong parent;
	u32 div, v;

	if (clk->id >= T30_CLK_COUNT)
		return -EINVAL;

	d = &t30_clks[clk->id];
	if (!d->cdr || !rate)
		return -ENOSYS;

	/* Source the leaf clock from MPLL (matches the vendor cgu_clks_set). */
	parent = pll_rate(p, CPM_CPMPCR);

	div = DIV_ROUND_CLOSEST(parent, rate);
	if (!div)
		div = 1;
	if (div > 256)
		div = 256;

	v = cpm_r(p, d->cdr);
	v &= ~(CDR_SRC_MASK | BIT(d->stop) | BIT(d->busy) | CDR_DIV_MASK);
	v |= CDR_SRC_MPLL | BIT(d->ce) | (div - 1);
	cpm_w(p, d->cdr, v);

	while (cpm_r(p, d->cdr) & BIT(d->busy))
		;

	return parent / div;
}

static int t30_clk_gate(struct clk *clk, bool enable)
{
	struct t30_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t30_clk_desc *d;

	if (clk->id >= T30_CLK_COUNT)
		return -EINVAL;

	d = &t30_clks[clk->id];
	if (d->gate_reg == NO_GATE || !d->gate_reg)
		return 0;

	/* The CLKGR bit is set to DISABLE the clock. */
	if (enable)
		clrbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));
	else
		setbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));

	return 0;
}

static int t30_clk_enable(struct clk *clk)
{
	return t30_clk_gate(clk, true);
}

static int t30_clk_disable(struct clk *clk)
{
	return t30_clk_gate(clk, false);
}

static int t30_clk_of_xlate(struct clk *clk,
			    struct ofnode_phandle_args *args)
{
	if (args->args_count != 1)
		return -EINVAL;

	clk->id = args->args[0];
	return 0;
}

static const struct clk_ops t30_clk_ops = {
	.of_xlate = t30_clk_of_xlate,
	.get_rate = t30_clk_get_rate,
	.set_rate = t30_clk_set_rate,
	.enable	  = t30_clk_enable,
	.disable  = t30_clk_disable,
};

static int t30_cgu_probe(struct udevice *dev)
{
	struct t30_cgu_priv *p = dev_get_priv(dev);

	p->base = (void __iomem *)T30_CPM_BASE;
	return 0;
}

static const struct udevice_id t30_cgu_ids[] = {
	{ .compatible = "ingenic,t30-cgu" },
	{ }
};

U_BOOT_DRIVER(ingenic_t30_cgu) = {
	.name		= "ingenic_t30_cgu",
	.id		= UCLASS_CLK,
	.of_match	= t30_cgu_ids,
	.probe		= t30_cgu_probe,
	.priv_auto	= sizeof(struct t30_cgu_priv),
	.ops		= &t30_clk_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
