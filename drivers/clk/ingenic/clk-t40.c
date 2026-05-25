// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T40 (XBurst2) CGU (Clock/Power Manager) clock driver.
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
#include <dt-bindings/clock/ingenic,t40-cgu.h>

#define T40_CLK_COUNT		(T40_CLK_OTG2 + 1)

/* CPM at physical 0x10000000, reached through the uncached KSEG1 window. */
#define A1_CPM_BASE		0xb0000000

#define CPM_CPAPCR		0x10	/* APLL */
#define CPM_CPMPCR		0x14	/* MPLL */
#define CPM_CPEPCR		0x18	/* EPLL */
#define CPM_CPVPCR		0x1c	/* VPLL */
#define CPM_CLKGR0		0x30
#define CPM_CLKGR1		0x38
#define CPM_SFC0CDR		0x90
#define CPM_MAC0CDR		0xc0
#define CPM_MAC1CDR		0xd0

#define EXT_RATE		24000000UL
#define RTC_RATE		32768UL

/* CDR source field [31:30]: 0=sclka(~apll) 1=mpll 2=vpll 3=epll */
#define CDR_SRC_SHIFT		30
#define CDR_SRC_MASK		(3u << CDR_SRC_SHIFT)
#define CDR_DIV_MASK		0xffu

struct a1_clk_desc {
	u16 cdr;	/* CPM CDR register offset, 0 = no divider */
	u8 ce;		/* clock-change-enable bit in cdr */
	u8 busy;	/* divider-busy bit in cdr */
	u8 stop;	/* clock-stop bit in cdr */
	u16 gate_reg;	/* CLKGR0/CLKGR1 offset, NO_GATE = no gate */
	u8 gate_bit;	/* gate bit (set = clock disabled) */
	u8 src;		/* CDR source select [31:30]: 1=MPLL, 3=EPLL */
};

#define NO_GATE 0xffff

/*
 * Leaf clocks U-Boot's drivers reference by the canonical binding ID.
 * SFC folds the SFC0 divider (SFC0CDR) and the SFC0 gate. The rest are
 * gate-only for now - their dividers are added as the consuming
 * drivers (MMC, GMAC) are ported.
 */
static const struct a1_clk_desc a1_clks[T40_CLK_COUNT] = {
	[T40_CLK_SFC]   = { CPM_SFC0CDR, 29, 28, 27, CPM_CLKGR0, 24, 1 },
	[T40_CLK_SFC1]  = { 0, 0, 0, 0, CPM_CLKGR0, 25 },
	[T40_CLK_MSC0]  = { 0, 0, 0, 0, CPM_CLKGR0, 14 },
	[T40_CLK_MSC1]  = { 0, 0, 0, 0, CPM_CLKGR0, 15 },
	[T40_CLK_UART0] = { 0, 0, 0, 0, CPM_CLKGR0, 8 },
	[T40_CLK_UART1] = { 0, 0, 0, 0, CPM_CLKGR0, 9 },
	[T40_CLK_UART2] = { 0, 0, 0, 0, CPM_CLKGR0, 10 },
	[T40_CLK_OTG0]  = { 0, 0, 0, 0, CPM_CLKGR0, 11 },
	[T40_CLK_OTG1]  = { 0, 0, 0, 0, CPM_CLKGR0, 12 },
	[T40_CLK_OTG2]  = { 0, 0, 0, 0, CPM_CLKGR0, 13 },
	[T40_CLK_TCU]   = { 0, 0, 0, 0, CPM_CLKGR0, 5 },
	[T40_CLK_OST]   = { 0, 0, 0, 0, CPM_CLKGR0, 6 },
	[T40_CLK_AIC]   = { 0, 0, 0, 0, CPM_CLKGR0, 30 },
	[T40_CLK_GMAC0] = { CPM_MAC0CDR, 29, 28, 27, CPM_CLKGR1, 8, 3 },
	[T40_CLK_GMAC1] = { CPM_MAC1CDR, 29, 28, 27, CPM_CLKGR1, 10, 3 },
	[T40_CLK_DMAC]  = { 0, 0, 0, 0, CPM_CLKGR1, 3 },
	[T40_CLK_EFUSE] = { 0, 0, 0, 0, CPM_CLKGR0, 4 },
};

struct t40_cgu_priv {
	void __iomem *base;
};

static u32 cpm_r(struct t40_cgu_priv *p, u32 off)
{
	return readl(p->base + off);
}

static void cpm_w(struct t40_cgu_priv *p, u32 off, u32 v)
{
	writel(v, p->base + off);
}

static ulong pll_rate(struct t40_cgu_priv *p, u32 reg)
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

static ulong a1_parent_rate(struct t40_cgu_priv *p, u32 cdr)
{
	switch ((cpm_r(p, cdr) & CDR_SRC_MASK) >> CDR_SRC_SHIFT) {
	case 1:
		return pll_rate(p, CPM_CPMPCR);	/* MPLL */
	case 2:
		return pll_rate(p, CPM_CPVPCR);	/* VPLL */
	case 3:
		return pll_rate(p, CPM_CPEPCR);	/* EPLL */
	default:
		return pll_rate(p, CPM_CPAPCR);	/* sclka ~ APLL */
	}
}

static ulong a1_clk_get_rate(struct clk *clk)
{
	struct t40_cgu_priv *p = dev_get_priv(clk->dev);
	const struct a1_clk_desc *d;

	switch (clk->id) {
	case T40_CLK_EXCLK:
		return EXT_RATE;
	case T40_CLK_RTCLK:
		return RTC_RATE;
	case T40_CLK_APLL:
		return pll_rate(p, CPM_CPAPCR);
	case T40_CLK_MPLL:
		return pll_rate(p, CPM_CPMPCR);
	case T40_CLK_EPLL:
		return pll_rate(p, CPM_CPEPCR);
	case T40_CLK_VPLL:
		return pll_rate(p, CPM_CPVPCR);
	case T40_CLK_UART0:
	case T40_CLK_UART1:
	case T40_CLK_UART2:
		return EXT_RATE;	/* A1 UART is clocked from EXT */
	}

	if (clk->id >= T40_CLK_COUNT)
		return -EINVAL;

	d = &a1_clks[clk->id];
	if (!d->cdr)
		return EXT_RATE;

	return a1_parent_rate(p, d->cdr) /
	       ((cpm_r(p, d->cdr) & CDR_DIV_MASK) + 1);
}

static ulong a1_clk_set_rate(struct clk *clk, ulong rate)
{
	struct t40_cgu_priv *p = dev_get_priv(clk->dev);
	const struct a1_clk_desc *d;
	ulong parent;
	u32 div, v;

	if (clk->id >= T40_CLK_COUNT)
		return -EINVAL;

	d = &a1_clks[clk->id];
	if (!d->cdr || !rate)
		return -ENOSYS;

	/* CDR source is the clock's fixed parent (SFC = MPLL, GMAC = EPLL). */
	switch (d->src) {
	case 1:
		parent = pll_rate(p, CPM_CPMPCR);	/* MPLL */
		break;
	case 2:
		parent = pll_rate(p, CPM_CPVPCR);	/* VPLL */
		break;
	case 3:
		parent = pll_rate(p, CPM_CPEPCR);	/* EPLL */
		break;
	default:
		parent = pll_rate(p, CPM_CPAPCR);	/* sclka ~ APLL */
		break;
	}

	div = DIV_ROUND_CLOSEST(parent, rate);
	if (!div)
		div = 1;
	if (div > 256)
		div = 256;

	v = cpm_r(p, d->cdr);
	v &= ~(CDR_SRC_MASK | BIT(d->stop) | BIT(d->busy) | CDR_DIV_MASK);
	v |= ((u32)d->src << CDR_SRC_SHIFT) | BIT(d->ce) | (div - 1);
	cpm_w(p, d->cdr, v);

	while (cpm_r(p, d->cdr) & BIT(d->busy))
		;

	return parent / div;
}

static int a1_clk_gate(struct clk *clk, bool enable)
{
	struct t40_cgu_priv *p = dev_get_priv(clk->dev);
	const struct a1_clk_desc *d;

	if (clk->id >= T40_CLK_COUNT)
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

static int t40_cgu_probe(struct udevice *dev)
{
	struct t40_cgu_priv *p = dev_get_priv(dev);

	p->base = (void __iomem *)A1_CPM_BASE;
	return 0;
}

static const struct udevice_id t40_cgu_ids[] = {
	{ .compatible = "ingenic,t40-cgu" },
	{ }
};

U_BOOT_DRIVER(ingenic_t40_cgu) = {
	.name		= "ingenic_t40_cgu",
	.id		= UCLASS_CLK,
	.of_match	= t40_cgu_ids,
	.probe		= t40_cgu_probe,
	.priv_auto	= sizeof(struct t40_cgu_priv),
	.ops		= &a1_clk_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
