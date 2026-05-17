// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 CGU (Clock/Power Manager) clock driver.
 *
 * The SPL brings up the PLLs and the CPU/DDR/bus dividers imperatively
 * (it is size-constrained and runs before DM); this driver runs in
 * U-Boot proper and models the leaf peripheral clocks the U-Boot
 * drivers actually consume: it recalculates the PLL rates from the CPM
 * registers and provides get_rate / set_rate / enable / disable for
 * SFC, MMC and the GMAC PHY clock, replacing the previous fixed-clock
 * device-tree fictions and the per-driver hand-poking of CPM.
 *
 * Register model and bit positions are taken from the Linux
 * drivers/clk/ingenic T31 driver and cross-checked against the proven
 * sequences already in ingenic_sfc.c / dwmac_ingenic.c. Note the
 * clock-change-enable / busy / stop bits are NOT in the same position
 * for every CDR register (SSICDR uses 28/27/26, MACCDR uses 29/28/27),
 * so each clock carries its own bit offsets.
 */

#include <clk-uclass.h>
#include <dm.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <dt-bindings/clock/ingenic,t31-cgu.h>

/*
 * Clock IDs are the canonical "ingenic,t31-cgu" binding (shared
 * verbatim with the mainline Linux port's dt-bindings header) so a
 * single device tree is valid on both Linux and U-Boot. The header
 * defines IDs up to T31_CLK_CE_I2SR; U-Boot only implements the leaf
 * clocks its drivers consume.
 */
#define T31_CLK_COUNT		(T31_CLK_CE_I2SR + 1)

/*
 * CPM is at physical 0x10000000; access it through the uncached MIPS
 * KSEG1 window, exactly like the other proven T31 drivers (ingenic_sfc,
 * dwmac_ingenic). Using the fixed window avoids the cached-MMIO /
 * ioremap pitfalls hit elsewhere on this SoC.
 */
#define T31_CPM_BASE		0xb0000000

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

struct t31_clk_desc {
	u16 cdr;	/* CPM CDR register offset, 0 = no divider */
	u8 ce;		/* clock-change-enable bit in cdr */
	u8 busy;	/* divider-busy bit in cdr */
	u8 stop;	/* clock-stop bit in cdr */
	u16 gate_reg;	/* CLKGR0/CLKGR1 offset, 0xffff = no gate */
	u8 gate_bit;	/* gate bit (set = clock disabled) */
};

#define NO_GATE 0xffff

/*
 * Leaf clocks U-Boot's drivers reference by the canonical binding ID.
 * SFC (T31_CLK_SFC) folds the SSIPLL divider (SSICDR) and the SFC
 * gate; GMAC (T31_CLK_GMAC) folds the MAC-PHY divider (MACCDR) and
 * the GMAC gate - the U-Boot consumers take a single clock each, so
 * the rate/gate are combined under the ID their DT node references
 * (matches the Linux port's per-consumer phandle).
 */
static const struct t31_clk_desc t31_clks[T31_CLK_COUNT] = {
	[T31_CLK_SFC]  = { CPM_SSICDR, 28, 27, 26, CPM_CLKGR0, 20 },
	[T31_CLK_MSC0] = { CPM_MSC0CDR, 29, 28, 27, CPM_CLKGR0, 4 },
	[T31_CLK_MSC1] = { CPM_MSC1CDR, 29, 28, 27, CPM_CLKGR0, 5 },
	[T31_CLK_GMAC] = { CPM_MACCDR, 29, 28, 27, CPM_CLKGR1, 4 },
	[T31_CLK_UART1] = { 0, 0, 0, 0, CPM_CLKGR0, 15 },
	[T31_CLK_OTG]  = { 0, 0, 0, 0, CPM_CLKGR0, 3 },
	[T31_CLK_TCU]  = { 0, 0, 0, 0, CPM_CLKGR0, 30 },
	[T31_CLK_OST]  = { 0, 0, 0, 0, CPM_CLKGR1, 11 },
};

struct t31_cgu_priv {
	void __iomem *base;
};

static u32 cpm_r(struct t31_cgu_priv *p, u32 off)
{
	return readl(p->base + off);
}

static void cpm_w(struct t31_cgu_priv *p, u32 off, u32 v)
{
	writel(v, p->base + off);
}

static ulong pll_rate(struct t31_cgu_priv *p, u32 reg)
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

static ulong t31_parent_rate(struct t31_cgu_priv *p, u32 cdr)
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

static ulong t31_clk_get_rate(struct clk *clk)
{
	struct t31_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t31_clk_desc *d;

	switch (clk->id) {
	case T31_CLK_EXCLK:
		return EXT_RATE;
	case T31_CLK_RTCLK:
		return RTC_RATE;
	case T31_CLK_APLL:
		return pll_rate(p, CPM_CPAPCR);
	case T31_CLK_MPLL:
		return pll_rate(p, CPM_CPMPCR);
	case T31_CLK_VPLL:
		return pll_rate(p, CPM_CPVPCR);
	case T31_CLK_UART1:
		return EXT_RATE;	/* T31 UART is clocked from EXT */
	}

	if (clk->id >= T31_CLK_COUNT)
		return -EINVAL;

	d = &t31_clks[clk->id];
	if (!d->cdr)
		return EXT_RATE;

	return t31_parent_rate(p, d->cdr) /
	       ((cpm_r(p, d->cdr) & CDR_DIV_MASK) + 1);
}

static ulong t31_clk_set_rate(struct clk *clk, ulong rate)
{
	struct t31_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t31_clk_desc *d;
	ulong parent;
	u32 div, v;

	if (clk->id >= T31_CLK_COUNT)
		return -EINVAL;

	d = &t31_clks[clk->id];
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

static int t31_clk_gate(struct clk *clk, bool enable)
{
	struct t31_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t31_clk_desc *d;

	if (clk->id >= T31_CLK_COUNT)
		return -EINVAL;

	d = &t31_clks[clk->id];
	if (d->gate_reg == NO_GATE || !d->gate_reg)
		return 0;

	/* The CLKGR bit is set to DISABLE the clock. */
	if (enable)
		clrbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));
	else
		setbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));

	return 0;
}

static int t31_clk_enable(struct clk *clk)
{
	return t31_clk_gate(clk, true);
}

static int t31_clk_disable(struct clk *clk)
{
	return t31_clk_gate(clk, false);
}

static int t31_clk_of_xlate(struct clk *clk,
			    struct ofnode_phandle_args *args)
{
	if (args->args_count != 1)
		return -EINVAL;

	clk->id = args->args[0];
	return 0;
}

static const struct clk_ops t31_clk_ops = {
	.of_xlate = t31_clk_of_xlate,
	.get_rate = t31_clk_get_rate,
	.set_rate = t31_clk_set_rate,
	.enable	  = t31_clk_enable,
	.disable  = t31_clk_disable,
};

static int t31_cgu_probe(struct udevice *dev)
{
	struct t31_cgu_priv *p = dev_get_priv(dev);

	p->base = (void __iomem *)T31_CPM_BASE;
	return 0;
}

static const struct udevice_id t31_cgu_ids[] = {
	{ .compatible = "ingenic,t31-cgu" },
	{ }
};

U_BOOT_DRIVER(ingenic_t31_cgu) = {
	.name		= "ingenic_t31_cgu",
	.id		= UCLASS_CLK,
	.of_match	= t31_cgu_ids,
	.probe		= t31_cgu_probe,
	.priv_auto	= sizeof(struct t31_cgu_priv),
	.ops		= &t31_clk_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
