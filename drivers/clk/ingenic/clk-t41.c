// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T41 (XBurst2) CGU (Clock/Power Manager) clock driver.
 *
 * The SPL brings up the PLLs and the CPU/DDR/bus dividers imperatively
 * (it is size-constrained and runs before DM); this driver runs in
 * U-Boot proper and models the leaf peripheral clocks the U-Boot
 * drivers actually consume: it recalculates the PLL rates from the CPM
 * registers and provides get_rate / set_rate / enable / disable.
 *
 * T41 sits next to T40 in the XBurst2 family. Both share the same
 * CGU block on paper, but the bits that matter for this driver
 * diverge enough to warrant a separate file:
 *   - PLLM is 9 bits not 12, OD0/OD1 sit at different positions, and
 *     a PLLRG[6:4] field is added. The vendor pll_get_rate formula
 *     is therefore EXTAL * (M+1) * 2 / ((N+1) * 2^OD0 * (OD1+1)).
 *   - CPM register offsets shift: CLKGR0/1 at 0x20/0x28 (T40 had 0x30/
 *     0x38), SFC0CDR=0x60 (was 0x90), MACCDR=0x54 (was 0xc0). T41 has
 *     a second SFC at SFC1CDR=0x7c but a single GMAC.
 *   - CLKGR bit map is entirely different (SFC0=21, GMAC0=CLKGR1.4,
 *     MSC0=4, OTG=3, AIC=11, TCU=30, etc).
 */

#include <clk-uclass.h>
#include <dm.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <dt-bindings/clock/ingenic,t41-cgu.h>

#define T41_CLK_COUNT		(T41_CLK_OTG2 + 1)

/* CPM base, reached through the uncached KSEG1 window. */
#define T41_CPM_BASE		0xb0000000

/* CPM map (vendor arch/mips/include/asm/arch-t41/cpm.h) */
#define CPM_CPAPCR		0x10	/* APLL */
#define CPM_CPMPCR		0x14	/* MPLL */
#define CPM_CPEPCR		0x18	/* EPLL */
#define CPM_CPVPCR		0x1c	/* VPLL */
#define CPM_CLKGR0		0x20
#define CPM_CLKGR1		0x28
#define CPM_MAC0CDR		0x54	/* MACCDR */
#define CPM_SFC0CDR		0x60
#define CPM_MSC0CDR		0x68
#define CPM_MSC1CDR		0x6c
#define CPM_SFC1CDR		0x7c

#define EXT_RATE		24000000UL
#define RTC_RATE		32768UL

/* CDR source field [31:30]: 0=sclka(~apll) 1=mpll 2=vpll 3=epll */
#define CDR_SRC_SHIFT		30
#define CDR_SRC_MASK		(3u << CDR_SRC_SHIFT)
#define CDR_DIV_MASK		0xffu

struct t41_clk_desc {
	u16 cdr;	/* CPM CDR register offset, 0 = no divider */
	u8 ce;		/* clock-change-enable bit in cdr */
	u8 busy;	/* divider-busy bit in cdr */
	u8 stop;	/* clock-stop bit in cdr */
	u16 gate_reg;	/* CLKGR0/CLKGR1 offset, NO_GATE = no gate */
	u8 gate_bit;	/* gate bit (set = clock disabled) */
	u8 src;		/* CDR source select [31:30]: 1=MPLL on T41 */
};

#define NO_GATE 0xffff

/*
 * T41 CLKGR bit map (vendor arch/mips/include/asm/arch-t41/cpm.h):
 *   CLKGR0: SFC0=21, MSC0=4, MSC1=5, OTG=3, AIC=11, TCU=30, EFUSE=1,
 *           UART0=14, UART1=15, UART2=16, PDMA=22
 *   CLKGR1: GMAC=4, SFC1=12, SYS_OST=11
 * Sources for GMAC/SFC MUX field [31:30]: 1=MPLL.
 */
static const struct t41_clk_desc t41_clks[T41_CLK_COUNT] = {
	[T41_CLK_SFC]   = { CPM_SFC0CDR, 29, 28, 27, CPM_CLKGR0, 21, 1 },
	[T41_CLK_SFC1]  = { CPM_SFC1CDR, 29, 28, 27, CPM_CLKGR1, 12, 1 },
	[T41_CLK_MSC0]  = { CPM_MSC0CDR, 29, 28, 27, CPM_CLKGR0, 4 },
	[T41_CLK_MSC1]  = { CPM_MSC1CDR, 29, 28, 27, CPM_CLKGR0, 5 },
	[T41_CLK_UART0] = { 0, 0, 0, 0, CPM_CLKGR0, 14 },
	[T41_CLK_UART1] = { 0, 0, 0, 0, CPM_CLKGR0, 15 },
	[T41_CLK_UART2] = { 0, 0, 0, 0, CPM_CLKGR0, 16 },
	[T41_CLK_OTG0]  = { 0, 0, 0, 0, CPM_CLKGR0, 3 },
	[T41_CLK_OTG1]  = { 0, 0, 0, 0, NO_GATE, 0 },	/* T41 has one OTG */
	[T41_CLK_OTG2]  = { 0, 0, 0, 0, NO_GATE, 0 },
	[T41_CLK_TCU]   = { 0, 0, 0, 0, CPM_CLKGR0, 30 },
	[T41_CLK_OST]   = { 0, 0, 0, 0, CPM_CLKGR1, 11 },
	[T41_CLK_AIC]   = { 0, 0, 0, 0, CPM_CLKGR0, 11 },
	[T41_CLK_GMAC0] = { CPM_MAC0CDR, 29, 28, 27, CPM_CLKGR1, 4, 1 },
	[T41_CLK_GMAC1] = { 0, 0, 0, 0, NO_GATE, 0 },	/* T41 has one GMAC */
	[T41_CLK_DMAC]  = { 0, 0, 0, 0, CPM_CLKGR0, 22 },	/* PDMA */
	[T41_CLK_EFUSE] = { 0, 0, 0, 0, CPM_CLKGR0, 1 },
};

struct t41_cgu_priv {
	void __iomem *base;
};

static u32 cpm_r(struct t41_cgu_priv *p, u32 off)
{
	return readl(p->base + off);
}

static void cpm_w(struct t41_cgu_priv *p, u32 off, u32 v)
{
	writel(v, p->base + off);
}

/*
 * T41 PLL register: PLLM[28:20] (9 bits), PLLN[19:14] (6 bits),
 * PLLOD0[13:11] (3 bits), PLLOD1[10:7] (4 bits), PLLRG[6:4] (3 bits).
 * Frequency = EXTAL * (M+1) * 2 / ((N+1) * 2^OD0 * (OD1+1)). The
 * (M+1)*EXTAL*2 product can exceed 2^32 (T41NQ: 24M * 350 * 2 = 16.8G),
 * so compute in u64 and divide back to ulong.
 */
static ulong pll_rate(struct t41_cgu_priv *p, u32 reg)
{
	u32 v = cpm_r(p, reg);
	u32 m = (v >> 20) & 0x1ff;
	u32 n = (v >> 14) & 0x3f;
	u32 od0 = (v >> 11) & 0x7;
	u32 od1 = (v >> 7) & 0xf;
	u64 vco = (u64)EXT_RATE * (m + 1) * 2;

	return (ulong)(vco / ((n + 1) * (1u << od0) * (od1 + 1)));
}

static ulong t41_parent_rate(struct t41_cgu_priv *p, u32 cdr)
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

static ulong t41_clk_get_rate(struct clk *clk)
{
	struct t41_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t41_clk_desc *d;

	switch (clk->id) {
	case T41_CLK_EXCLK:
		return EXT_RATE;
	case T41_CLK_RTCLK:
		return RTC_RATE;
	case T41_CLK_APLL:
		return pll_rate(p, CPM_CPAPCR);
	case T41_CLK_MPLL:
		return pll_rate(p, CPM_CPMPCR);
	case T41_CLK_EPLL:
		return pll_rate(p, CPM_CPEPCR);
	case T41_CLK_VPLL:
		return pll_rate(p, CPM_CPVPCR);
	case T41_CLK_UART0:
	case T41_CLK_UART1:
	case T41_CLK_UART2:
		return EXT_RATE;	/* T41 UART is clocked from EXTAL */
	}

	if (clk->id >= T41_CLK_COUNT)
		return -EINVAL;

	d = &t41_clks[clk->id];
	if (!d->cdr)
		return EXT_RATE;

	return t41_parent_rate(p, d->cdr) /
	       ((cpm_r(p, d->cdr) & CDR_DIV_MASK) + 1);
}

static ulong t41_clk_set_rate(struct clk *clk, ulong rate)
{
	struct t41_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t41_clk_desc *d;
	ulong parent;
	u32 div, v;

	if (clk->id >= T41_CLK_COUNT)
		return -EINVAL;

	d = &t41_clks[clk->id];
	if (!d->cdr || !rate)
		return -ENOSYS;

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

static int t41_clk_gate(struct clk *clk, bool enable)
{
	struct t41_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t41_clk_desc *d;

	if (clk->id >= T41_CLK_COUNT)
		return -EINVAL;

	d = &t41_clks[clk->id];
	if (d->gate_reg == NO_GATE || !d->gate_reg)
		return 0;

	/* The CLKGR bit is set to DISABLE the clock. */
	if (enable)
		clrbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));
	else
		setbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));

	return 0;
}

static int t41_clk_enable(struct clk *clk)
{
	return t41_clk_gate(clk, true);
}

static int t41_clk_disable(struct clk *clk)
{
	return t41_clk_gate(clk, false);
}

static int t41_clk_of_xlate(struct clk *clk,
			    struct ofnode_phandle_args *args)
{
	if (args->args_count != 1)
		return -EINVAL;

	clk->id = args->args[0];
	return 0;
}

static const struct clk_ops t41_clk_ops = {
	.of_xlate = t41_clk_of_xlate,
	.get_rate = t41_clk_get_rate,
	.set_rate = t41_clk_set_rate,
	.enable	  = t41_clk_enable,
	.disable  = t41_clk_disable,
};

static int t41_cgu_probe(struct udevice *dev)
{
	struct t41_cgu_priv *p = dev_get_priv(dev);

	p->base = (void __iomem *)T41_CPM_BASE;
	return 0;
}

static const struct udevice_id t41_cgu_ids[] = {
	{ .compatible = "ingenic,t41-cgu" },
	{ }
};

U_BOOT_DRIVER(ingenic_t41_cgu) = {
	.name		= "ingenic_t41_cgu",
	.id		= UCLASS_CLK,
	.of_match	= t41_cgu_ids,
	.probe		= t41_cgu_probe,
	.priv_auto	= sizeof(struct t41_cgu_priv),
	.ops		= &t41_clk_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
