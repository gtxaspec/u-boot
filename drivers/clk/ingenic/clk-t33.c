// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T33 CGU (Clock/Power Manager) clock driver.
 *
 * The SPL brings up the PLLs and the CPU/DDR/bus dividers imperatively
 * (it is size-constrained and runs before DM); this driver runs in
 * U-Boot proper and models the leaf peripheral clocks the U-Boot
 * drivers actually consume: it recalculates the PLL rates from the CPM
 * registers and provides get_rate / set_rate / enable / disable for
 * SFC, MMC and the GMAC clock.
 *
 * T33 (PRJ008) is "T31-extended" but the CGU is NOT bit-compatible
 * with T31: the SFC clock is a dedicated divider (CPM_SFC0CDR, not the
 * shared SSICDR), every CDR uses ce/busy/stop = 29/28/27 (T31's SSI
 * used 28/27/26), and the CLKGR* gate bit map is different (SFC =
 * CLKGR0 bit 17, GMAC = CLKGR1 bit 0, ...). Register model and bit
 * positions are taken from the vendor U-Boot 2022.10 arch-PRJ cpm.h
 * and cgu_clk_sel[] table, not mirrored from the T31 driver.
 */

#include <clk-uclass.h>
#include <dm.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <dt-bindings/clock/ingenic,t33-cgu.h>

#define T33_CLK_COUNT		(T33_CLK_USBPHY + 1)

/* CPM at physical 0x10000000, accessed through the uncached KSEG1 window. */
#define T33_CPM_BASE		0xb0000000

#define CPM_CPCCR		0x00
#define CPM_CPAPCR		0x10	/* APLL */
#define CPM_CPMPCR		0x14	/* MPLL */
#define CPM_CLKGR0		0x20
#define CPM_CLKGR1		0x28
#define CPM_MACCDR		0x54
#define CPM_SFC0CDR		0x58
#define CPM_MSC0CDR		0x68
#define CPM_MSC1CDR		0xa4
#define CPM_CPVPCR		0xe0	/* VPLL */

#define EXT_RATE		24000000UL
#define RTC_RATE		32768UL

/* CDR source field [31:30]: 0=APLL 1=MPLL 2=VPLL */
#define CDR_SRC_SHIFT		30
#define CDR_SRC_MASK		(3u << CDR_SRC_SHIFT)
#define CDR_SRC_MPLL		(1u << CDR_SRC_SHIFT)
#define CDR_DIV_MASK		0xffu

struct t33_clk_desc {
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
 * All T33 CDRs use ce/busy/stop = 29/28/27. Gate bits (set = disabled):
 * CLKGR0 - SFC0 17, MSC0 3, MSC1 4, OTG 2, UART0 11, UART1 12, TCU 26;
 * CLKGR1 - GMAC 0, OST 7.
 */
static const struct t33_clk_desc t33_clks[T33_CLK_COUNT] = {
	[T33_CLK_SFC]   = { CPM_SFC0CDR, 29, 28, 27, CPM_CLKGR0, 17 },
	[T33_CLK_MSC0]  = { CPM_MSC0CDR, 29, 28, 27, CPM_CLKGR0, 3 },
	[T33_CLK_MSC1]  = { CPM_MSC1CDR, 29, 28, 27, CPM_CLKGR0, 4 },
	[T33_CLK_GMAC]  = { CPM_MACCDR, 29, 28, 27, CPM_CLKGR1, 0 },
	[T33_CLK_UART0] = { 0, 0, 0, 0, CPM_CLKGR0, 11 },
	[T33_CLK_UART1] = { 0, 0, 0, 0, CPM_CLKGR0, 12 },
	[T33_CLK_OTG]   = { 0, 0, 0, 0, CPM_CLKGR0, 2 },
	[T33_CLK_TCU]   = { 0, 0, 0, 0, CPM_CLKGR0, 26 },
	[T33_CLK_OST]   = { 0, 0, 0, 0, CPM_CLKGR1, 7 },
};

struct t33_cgu_priv {
	void __iomem *base;
};

static u32 cpm_r(struct t33_cgu_priv *p, u32 off)
{
	return readl(p->base + off);
}

static void cpm_w(struct t33_cgu_priv *p, u32 off, u32 v)
{
	writel(v, p->base + off);
}

static ulong pll_rate(struct t33_cgu_priv *p, u32 reg)
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

static ulong t33_parent_rate(struct t33_cgu_priv *p, u32 cdr)
{
	switch ((cpm_r(p, cdr) & CDR_SRC_MASK) >> CDR_SRC_SHIFT) {
	case 1:
		return pll_rate(p, CPM_CPMPCR);	/* MPLL */
	case 2:
		return pll_rate(p, CPM_CPVPCR);	/* VPLL */
	default:
		return pll_rate(p, CPM_CPAPCR);	/* APLL */
	}
}

static ulong t33_clk_get_rate(struct clk *clk)
{
	struct t33_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t33_clk_desc *d;

	switch (clk->id) {
	case T33_CLK_EXCLK:
		return EXT_RATE;
	case T33_CLK_RTCLK:
		return RTC_RATE;
	case T33_CLK_APLL:
		return pll_rate(p, CPM_CPAPCR);
	case T33_CLK_MPLL:
		return pll_rate(p, CPM_CPMPCR);
	case T33_CLK_VPLL:
		return pll_rate(p, CPM_CPVPCR);
	case T33_CLK_UART0:
	case T33_CLK_UART1:
		return EXT_RATE;	/* T33 UART is clocked from EXT */
	}

	if (clk->id >= T33_CLK_COUNT)
		return -EINVAL;

	d = &t33_clks[clk->id];
	if (!d->cdr)
		return EXT_RATE;

	return t33_parent_rate(p, d->cdr) /
	       ((cpm_r(p, d->cdr) & CDR_DIV_MASK) + 1);
}

static ulong t33_clk_set_rate(struct clk *clk, ulong rate)
{
	struct t33_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t33_clk_desc *d;
	ulong parent;
	u32 div, v;

	if (clk->id >= T33_CLK_COUNT)
		return -EINVAL;

	d = &t33_clks[clk->id];
	if (!d->cdr || !rate)
		return -ENOSYS;

	/* Source the leaf clock from MPLL (matches the vendor cgu table). */
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

static int t33_clk_gate(struct clk *clk, bool enable)
{
	struct t33_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t33_clk_desc *d;

	if (clk->id >= T33_CLK_COUNT)
		return -EINVAL;

	d = &t33_clks[clk->id];
	if (d->gate_reg == NO_GATE || !d->gate_reg)
		return 0;

	/* The CLKGR bit is set to DISABLE the clock. */
	if (enable)
		clrbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));
	else
		setbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));

	return 0;
}

static int t33_clk_enable(struct clk *clk)
{
	return t33_clk_gate(clk, true);
}

static int t33_clk_disable(struct clk *clk)
{
	return t33_clk_gate(clk, false);
}

static int t33_clk_of_xlate(struct clk *clk,
			    struct ofnode_phandle_args *args)
{
	if (args->args_count != 1)
		return -EINVAL;

	clk->id = args->args[0];
	return 0;
}

static const struct clk_ops t33_clk_ops = {
	.of_xlate = t33_clk_of_xlate,
	.get_rate = t33_clk_get_rate,
	.set_rate = t33_clk_set_rate,
	.enable	  = t33_clk_enable,
	.disable  = t33_clk_disable,
};

static int t33_cgu_probe(struct udevice *dev)
{
	struct t33_cgu_priv *p = dev_get_priv(dev);

	p->base = (void __iomem *)T33_CPM_BASE;
	return 0;
}

static const struct udevice_id t33_cgu_ids[] = {
	{ .compatible = "ingenic,t33-cgu" },
	{ }
};

U_BOOT_DRIVER(ingenic_t33_cgu) = {
	.name		= "ingenic_t33_cgu",
	.id		= UCLASS_CLK,
	.of_match	= t33_cgu_ids,
	.probe		= t33_cgu_probe,
	.priv_auto	= sizeof(struct t33_cgu_priv),
	.ops		= &t33_clk_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
