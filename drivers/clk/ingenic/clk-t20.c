// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T20 CGU (Clock/Power Manager) clock driver.
 *
 * The SPL brings up the PLLs and the CPU/DDR/bus dividers imperatively
 * (it is size-constrained and runs before DM); this driver runs in
 * U-Boot proper and models the leaf peripheral clocks the U-Boot
 * drivers actually consume: it recalculates the PLL rates from the CPM
 * registers and provides get_rate / set_rate / enable / disable for
 * SFC, MMC and the GMAC PHY clock.
 *
 * T20 uses the T31/T23-style M/N/OD1/OD0 PLL encoding (NOT the
 * cpm_cpxpcr_t form of T21/T30). T20 has a VPLL, but this driver only
 * models the leaf clocks (SFC/MMC/GMAC), which source sclka(~APLL) or
 * MPLL, so the VPLL is not implemented here.
 *
 * IMPORTANT - T20 CGU layout differs from T31/T23/T30. From the
 * vendor t20/clk.c cgu_clk_sel[] table:
 *   SSI(SFC) = {CPM_SSICDR, sel bit 31 (1-bit, 0=APLL 1=MPLL),
 *               ce 29, busy 28, stop 27}
 *   MSC0     = {CPM_MSC0CDR, sel bit 31 (1-bit), ce 29, busy 28,
 *               stop 27}
 *   MACPHY   = {CPM_MACCDR, sel [31:30] (2-bit, 0=APLL 1=MPLL
 *               2=VPLL), ce 29, busy 28, stop 27}
 * i.e. the CDR clock-enable/busy/stop are 29/28/27 for every leaf
 * (not 28/27/26 as on T31/T23 SSICDR), and SSI/MSC select the PLL
 * with a single bit 31 while MAC uses the [31:30] pair. So each
 * clock carries its own bit offsets and source-field width.
 */

#include <clk-uclass.h>
#include <dm.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <dt-bindings/clock/ingenic,t20-cgu.h>

#define T20_CLK_COUNT		(T20_CLK_CE_I2SR + 1)

/*
 * CPM is at physical 0x10000000; access it through the uncached MIPS
 * KSEG1 window, exactly like the other proven drivers (ingenic_sfc,
 * dwmac_ingenic).
 */
#define T20_CPM_BASE		0xb0000000

#define CPM_CPCCR		0x00
#define CPM_CPAPCR		0x10	/* APLL */
#define CPM_CPMPCR		0x14	/* MPLL */
#define CPM_CLKGR0		0x20
#define CPM_CLKGR1		0x28
#define CPM_MACCDR		0x54
#define CPM_MSC0CDR		0x68
#define CPM_SSICDR		0x74

#define EXT_RATE		24000000UL
#define RTC_RATE		32768UL

#define CDR_DIV_MASK		0xffu

struct t20_clk_desc {
	u16 cdr;	/* CPM CDR register offset, 0 = no divider */
	u8 ce;		/* clock-change-enable bit in cdr */
	u8 busy;	/* divider-busy bit in cdr */
	u8 stop;	/* clock-stop bit in cdr */
	u8 src_shift;	/* PLL-select field shift in cdr */
	u8 src_2bit;	/* 1 = [shift+1:shift] (0=APLL 1=MPLL 2=VPLL),
			 * 0 = single bit (0=APLL 1=MPLL) */
	u16 gate_reg;	/* CLKGR0/CLKGR1 offset, 0xffff = no gate */
	u8 gate_bit;	/* gate bit (set = clock disabled) */
};

#define NO_GATE 0xffff

/*
 * Leaf clocks U-Boot's drivers reference by the canonical binding ID.
 * Bit offsets are the exact vendor t20/clk.c cgu_clk_sel[] entries
 * (SSI/MSC src = single bit 31; MAC src = [31:30]; ce/busy/stop =
 * 29/28/27 for all three). T20 has a single MSC (MSC0).
 */
static const struct t20_clk_desc t20_clks[T20_CLK_COUNT] = {
	[T20_CLK_SFC]  = { CPM_SSICDR, 29, 28, 27, 31, 0, CPM_CLKGR0, 20 },
	[T20_CLK_MSC0] = { CPM_MSC0CDR, 29, 28, 27, 31, 0, CPM_CLKGR0, 4 },
	[T20_CLK_GMAC] = { CPM_MACCDR, 29, 28, 27, 30, 1, CPM_CLKGR1, 4 },
	[T20_CLK_UART1] = { 0, 0, 0, 0, 0, 0, CPM_CLKGR0, 15 },
	[T20_CLK_OTG]  = { 0, 0, 0, 0, 0, 0, CPM_CLKGR0, 3 },
	[T20_CLK_TCU]  = { 0, 0, 0, 0, 0, 0, CPM_CLKGR0, 30 },
	[T20_CLK_OST]  = { 0, 0, 0, 0, 0, 0, CPM_CLKGR1, 11 },
};

struct t20_cgu_priv {
	void __iomem *base;
};

static u32 cpm_r(struct t20_cgu_priv *p, u32 off)
{
	return readl(p->base + off);
}

static void cpm_w(struct t20_cgu_priv *p, u32 off, u32 v)
{
	writel(v, p->base + off);
}

static ulong pll_rate(struct t20_cgu_priv *p, u32 reg)
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

/* Decode the PLL-select field: returns true if the leaf is on MPLL. */
static bool cdr_is_mpll(struct t20_cgu_priv *p, const struct t20_clk_desc *d)
{
	u32 v = cpm_r(p, d->cdr);

	if (d->src_2bit)
		return ((v >> d->src_shift) & 0x3) == 1;	/* 1 = MPLL */
	return (v >> d->src_shift) & 0x1;			/* 1 = MPLL */
}

static ulong t20_parent_rate(struct t20_cgu_priv *p,
			     const struct t20_clk_desc *d)
{
	if (cdr_is_mpll(p, d))
		return pll_rate(p, CPM_CPMPCR);		/* MPLL */
	return pll_rate(p, CPM_CPAPCR);			/* sclka ~ APLL */
}

static ulong t20_clk_get_rate(struct clk *clk)
{
	struct t20_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t20_clk_desc *d;

	switch (clk->id) {
	case T20_CLK_EXCLK:
		return EXT_RATE;
	case T20_CLK_RTCLK:
		return RTC_RATE;
	case T20_CLK_APLL:
		return pll_rate(p, CPM_CPAPCR);
	case T20_CLK_MPLL:
		return pll_rate(p, CPM_CPMPCR);
	case T20_CLK_UART1:
		return EXT_RATE;	/* T20 UART is clocked from EXT */
	}

	if (clk->id >= T20_CLK_COUNT)
		return -EINVAL;

	d = &t20_clks[clk->id];
	if (!d->cdr)
		return EXT_RATE;

	return t20_parent_rate(p, d) /
	       ((cpm_r(p, d->cdr) & CDR_DIV_MASK) + 1);
}

static ulong t20_clk_set_rate(struct clk *clk, ulong rate)
{
	struct t20_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t20_clk_desc *d;
	ulong parent;
	u32 div, v, src_mask, src_mpll;

	if (clk->id >= T20_CLK_COUNT)
		return -EINVAL;

	d = &t20_clks[clk->id];
	if (!d->cdr || !rate)
		return -ENOSYS;

	/* Source the leaf clock from MPLL (matches the vendor cgu set). */
	parent = pll_rate(p, CPM_CPMPCR);

	div = DIV_ROUND_CLOSEST(parent, rate);
	if (!div)
		div = 1;
	if (div > 256)
		div = 256;

	if (d->src_2bit) {
		src_mask = 0x3u << d->src_shift;
		src_mpll = 1u << d->src_shift;		/* [.. :..] = 01 */
	} else {
		src_mask = 0x1u << d->src_shift;
		src_mpll = 1u << d->src_shift;		/* single bit = 1 */
	}

	v = cpm_r(p, d->cdr);
	v &= ~(src_mask | BIT(d->stop) | BIT(d->busy) | CDR_DIV_MASK);
	v |= src_mpll | BIT(d->ce) | (div - 1);
	cpm_w(p, d->cdr, v);

	while (cpm_r(p, d->cdr) & BIT(d->busy))
		;

	return parent / div;
}

static int t20_clk_gate(struct clk *clk, bool enable)
{
	struct t20_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t20_clk_desc *d;

	if (clk->id >= T20_CLK_COUNT)
		return -EINVAL;

	d = &t20_clks[clk->id];
	if (d->gate_reg == NO_GATE || !d->gate_reg)
		return 0;

	/* The CLKGR bit is set to DISABLE the clock. */
	if (enable)
		clrbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));
	else
		setbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));

	return 0;
}

static int t20_clk_enable(struct clk *clk)
{
	return t20_clk_gate(clk, true);
}

static int t20_clk_disable(struct clk *clk)
{
	return t20_clk_gate(clk, false);
}

static int t20_clk_of_xlate(struct clk *clk,
			    struct ofnode_phandle_args *args)
{
	if (args->args_count != 1)
		return -EINVAL;

	clk->id = args->args[0];
	return 0;
}

static const struct clk_ops t20_clk_ops = {
	.of_xlate = t20_clk_of_xlate,
	.get_rate = t20_clk_get_rate,
	.set_rate = t20_clk_set_rate,
	.enable	  = t20_clk_enable,
	.disable  = t20_clk_disable,
};

static int t20_cgu_probe(struct udevice *dev)
{
	struct t20_cgu_priv *p = dev_get_priv(dev);

	p->base = (void __iomem *)T20_CPM_BASE;
	return 0;
}

static const struct udevice_id t20_cgu_ids[] = {
	{ .compatible = "ingenic,t20-cgu" },
	{ }
};

U_BOOT_DRIVER(ingenic_t20_cgu) = {
	.name		= "ingenic_t20_cgu",
	.id		= UCLASS_CLK,
	.of_match	= t20_cgu_ids,
	.probe		= t20_cgu_probe,
	.priv_auto	= sizeof(struct t20_cgu_priv),
	.ops		= &t20_clk_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
