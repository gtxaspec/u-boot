// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 (XBurst2) CGU (Clock/Power Manager) clock driver.
 *
 * The SPL brings up the PLLs and the CPU/DDR/bus dividers imperatively
 * (it is size-constrained and runs before DM); this driver runs in
 * U-Boot proper and models the leaf peripheral clocks the U-Boot
 * drivers actually consume: it recalculates the PLL rates from the CPM
 * registers and provides get_rate / set_rate / enable / disable.
 *
 * Same M/N/OD1/OD0 PLL encoding as the XBurst1 T-series; the CGU
 * register map differs (A1 CLKGR0=0x30/CLKGR1=0x38, dedicated SFC0
 * divider at 0x90). Bit positions are taken from the A1 SPL loader
 * (arch/mips/mach-xburst/a1/sfc.c) and the vendor cgu_clk_sel[] table.
 */

#include <clk-uclass.h>
#include <dm.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <dt-bindings/clock/ingenic,a1-cgu.h>

#define A1_CLK_COUNT		(A1_CLK_EFUSE + 1)

/* CPM at physical 0x10000000, reached through the uncached KSEG1 window. */
#define A1_CPM_BASE		0xb0000000

#define CPM_CPAPCR		0x10	/* APLL */
#define CPM_CPMPCR		0x14	/* MPLL */
#define CPM_CPEPCR		0x18	/* EPLL */
#define CPM_CPVPCR		0x1c	/* VPLL */
#define CPM_CLKGR0		0x30
#define CPM_CLKGR1		0x38
#define CPM_SFC0CDR		0x90

#define EXT_RATE		24000000UL
#define RTC_RATE		32768UL

/* CDR source field [31:30]: 0=sclka(~apll) 1=mpll 2=vpll */
#define CDR_SRC_SHIFT		30
#define CDR_SRC_MASK		(3u << CDR_SRC_SHIFT)
#define CDR_SRC_MPLL		(1u << CDR_SRC_SHIFT)
#define CDR_DIV_MASK		0xffu

struct a1_clk_desc {
	u16 cdr;	/* CPM CDR register offset, 0 = no divider */
	u8 ce;		/* clock-change-enable bit in cdr */
	u8 busy;	/* divider-busy bit in cdr */
	u8 stop;	/* clock-stop bit in cdr */
	u16 gate_reg;	/* CLKGR0/CLKGR1 offset, NO_GATE = no gate */
	u8 gate_bit;	/* gate bit (set = clock disabled) */
};

#define NO_GATE 0xffff

/*
 * Leaf clocks U-Boot's drivers reference by the canonical binding ID.
 * SFC folds the SFC0 divider (SFC0CDR) and the SFC0 gate. The rest are
 * gate-only for now - their dividers are added as the consuming
 * drivers (MMC, GMAC) are ported.
 */
static const struct a1_clk_desc a1_clks[A1_CLK_COUNT] = {
	[A1_CLK_SFC]   = { CPM_SFC0CDR, 28, 27, 26, CPM_CLKGR0, 24 },
	[A1_CLK_SFC1]  = { 0, 0, 0, 0, CPM_CLKGR0, 25 },
	[A1_CLK_MSC0]  = { 0, 0, 0, 0, CPM_CLKGR0, 14 },
	[A1_CLK_MSC1]  = { 0, 0, 0, 0, CPM_CLKGR0, 15 },
	[A1_CLK_UART0] = { 0, 0, 0, 0, CPM_CLKGR0, 8 },
	[A1_CLK_UART1] = { 0, 0, 0, 0, CPM_CLKGR0, 9 },
	[A1_CLK_UART2] = { 0, 0, 0, 0, CPM_CLKGR0, 10 },
	[A1_CLK_OTG]   = { 0, 0, 0, 0, CPM_CLKGR0, 11 },
	[A1_CLK_TCU]   = { 0, 0, 0, 0, CPM_CLKGR0, 5 },
	[A1_CLK_OST]   = { 0, 0, 0, 0, CPM_CLKGR0, 6 },
	[A1_CLK_AIC]   = { 0, 0, 0, 0, CPM_CLKGR0, 30 },
	[A1_CLK_GMAC0] = { 0, 0, 0, 0, CPM_CLKGR1, 8 },
	[A1_CLK_GMAC1] = { 0, 0, 0, 0, CPM_CLKGR1, 10 },
	[A1_CLK_DMAC]  = { 0, 0, 0, 0, CPM_CLKGR1, 3 },
	[A1_CLK_EFUSE] = { 0, 0, 0, 0, CPM_CLKGR0, 4 },
};

struct a1_cgu_priv {
	void __iomem *base;
};

static u32 cpm_r(struct a1_cgu_priv *p, u32 off)
{
	return readl(p->base + off);
}

static void cpm_w(struct a1_cgu_priv *p, u32 off, u32 v)
{
	writel(v, p->base + off);
}

static ulong pll_rate(struct a1_cgu_priv *p, u32 reg)
{
	u32 v = cpm_r(p, reg);
	u32 m = (v >> 20) & 0xfff;
	u32 n = (v >> 14) & 0x3f;
	u32 od1 = (v >> 11) & 0x7;
	u32 od0 = (v >> 8) & 0x7;
	u64 rate = (u64)EXT_RATE * m;

	if (!n)
		n = 1;
	if (!od1)
		od1 = 1;
	if (!od0)
		od0 = 1;

	return (ulong)(rate / n / od1 / od0);
}

static ulong a1_parent_rate(struct a1_cgu_priv *p, u32 cdr)
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

static ulong a1_clk_get_rate(struct clk *clk)
{
	struct a1_cgu_priv *p = dev_get_priv(clk->dev);
	const struct a1_clk_desc *d;

	switch (clk->id) {
	case A1_CLK_EXCLK:
		return EXT_RATE;
	case A1_CLK_RTCLK:
		return RTC_RATE;
	case A1_CLK_APLL:
		return pll_rate(p, CPM_CPAPCR);
	case A1_CLK_MPLL:
		return pll_rate(p, CPM_CPMPCR);
	case A1_CLK_EPLL:
		return pll_rate(p, CPM_CPEPCR);
	case A1_CLK_VPLL:
		return pll_rate(p, CPM_CPVPCR);
	case A1_CLK_UART0:
	case A1_CLK_UART1:
	case A1_CLK_UART2:
		return EXT_RATE;	/* A1 UART is clocked from EXT */
	}

	if (clk->id >= A1_CLK_COUNT)
		return -EINVAL;

	d = &a1_clks[clk->id];
	if (!d->cdr)
		return EXT_RATE;

	return a1_parent_rate(p, d->cdr) /
	       ((cpm_r(p, d->cdr) & CDR_DIV_MASK) + 1);
}

static ulong a1_clk_set_rate(struct clk *clk, ulong rate)
{
	struct a1_cgu_priv *p = dev_get_priv(clk->dev);
	const struct a1_clk_desc *d;
	ulong parent;
	u32 div, v;

	if (clk->id >= A1_CLK_COUNT)
		return -EINVAL;

	d = &a1_clks[clk->id];
	if (!d->cdr || !rate)
		return -ENOSYS;

	/* Source the leaf clock from MPLL (matches the vendor cgu set). */
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

static int a1_clk_gate(struct clk *clk, bool enable)
{
	struct a1_cgu_priv *p = dev_get_priv(clk->dev);
	const struct a1_clk_desc *d;

	if (clk->id >= A1_CLK_COUNT)
		return -EINVAL;

	d = &a1_clks[clk->id];
	if (d->gate_reg == NO_GATE || !d->gate_reg)
		return 0;

	/* The CLKGR bit is set to DISABLE the clock. */
	if (enable)
		clrbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));
	else
		setbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));

	return 0;
}

static int a1_clk_enable(struct clk *clk)
{
	return a1_clk_gate(clk, true);
}

static int a1_clk_disable(struct clk *clk)
{
	return a1_clk_gate(clk, false);
}

static int a1_clk_of_xlate(struct clk *clk,
			   struct ofnode_phandle_args *args)
{
	if (args->args_count != 1)
		return -EINVAL;

	clk->id = args->args[0];
	return 0;
}

static const struct clk_ops a1_clk_ops = {
	.of_xlate = a1_clk_of_xlate,
	.get_rate = a1_clk_get_rate,
	.set_rate = a1_clk_set_rate,
	.enable	  = a1_clk_enable,
	.disable  = a1_clk_disable,
};

static int a1_cgu_probe(struct udevice *dev)
{
	struct a1_cgu_priv *p = dev_get_priv(dev);

	p->base = (void __iomem *)A1_CPM_BASE;
	return 0;
}

static const struct udevice_id a1_cgu_ids[] = {
	{ .compatible = "ingenic,a1-cgu" },
	{ }
};

U_BOOT_DRIVER(ingenic_a1_cgu) = {
	.name		= "ingenic_a1_cgu",
	.id		= UCLASS_CLK,
	.of_match	= a1_cgu_ids,
	.probe		= a1_cgu_probe,
	.priv_auto	= sizeof(struct a1_cgu_priv),
	.ops		= &a1_clk_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
