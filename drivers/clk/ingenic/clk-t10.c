// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T10 CGU (Clock/Power Manager) clock driver.
 *
 * The SPL brings up the PLLs and the CPU/DDR/bus dividers imperatively
 * (it is size-constrained and runs before DM); this driver runs in
 * U-Boot proper and models the leaf peripheral clocks the U-Boot
 * drivers actually consume: it recalculates the PLL rates from the CPM
 * registers and provides get_rate / set_rate / enable / disable for
 * SFC, MMC and the GMAC PHY clock.
 *
 * T10 uses the T31/T23-style M/N/OD1/OD0 PLL encoding (NOT the
 * cpm_cpxpcr_t form of T21/T30). T10 has a VPLL, but this driver only
 * models the leaf clocks (SFC/MMC/GMAC), which source sclka(~APLL) or
 * MPLL, so the VPLL is not implemented here.
 *
 * IMPORTANT - T10 CGU layout differs from T31/T23/T30. From the
 * vendor t10/clk.c cgu_clk_sel[] table:
 *   SSI(SFC) = {CPM_SSICDR, sel bit 31 (1-bit, 0=APLL 1=MPLL),
 *               ce 29, busy 28, stop 27}
 *   MSC0     = {CPM_MSC0CDR, sel bit 31 (1-bit), ce 29, busy 28,
 *               stop 27}
 *   MACPHY   = {CPM_MACCDR, sel bit 31 (1-bit, 0=APLL 1=MPLL),
 *               ce 29, busy 28, stop 27}
 * i.e. the CDR clock-enable/busy/stop are 29/28/27 for every leaf
 * (not 28/27/26 as on T31/T23 SSICDR), and ALL of SSI/MSC/MAC
 * select the PLL with a single bit 31 (more uniform than T20,
 * whose MAC used the [31:30] pair). So each clock carries its own
 * bit offsets and source-field width.
 */

#include <clk-uclass.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <dt-bindings/clock/ingenic,t10-cgu.h>

#define T10_CLK_COUNT		(T10_CLK_VPU + 1)

/*
 * CPM is at physical 0x10000000; access it through the uncached MIPS
 * KSEG1 window, exactly like the other proven drivers (ingenic_sfc,
 * dwmac_ingenic).
 */
#define T10_CPM_BASE		0xb0000000

#define CPM_CPCCR		0x00
#define CPM_CPAPCR		0x10	/* APLL */
#define CPM_CPMPCR		0x14	/* MPLL */
#define CPM_CLKGR0		0x20
#define CPM_CLKGR1		0x28
#define CPM_VPUCDR		0x30
#define CPM_MACCDR		0x54
#define CPM_MSC0CDR		0x68
#define CPM_SSICDR		0x74
#define CPM_CIMCDR		0x7c
#define CPM_ISPCDR		0x80


/* CPxPCR PLL lock status (bit 3 = PLL stable). */
#define PLL_ON			BIT(3)

/* Source-mux input indices (2-bit fields; 1-bit fields allow 0/1). */
#define SRC_SCLKA		0
#define SRC_MPLL		1
#define SRC_VPLL		2

#define EXT_RATE		24000000UL
#define RTC_RATE		32768UL

#define CDR_DIV_MASK		0xffu

struct t10_clk_desc {
	u16 cdr;	/* CPM CDR register offset, 0 = no divider */
	u8 ce;		/* clock-change-enable bit in cdr */
	u8 busy;	/* divider-busy bit in cdr */
	u8 stop;	/* clock-stop bit in cdr */
	u8 src_shift;	/* PLL-select field shift in cdr */
	u8 src_2bit;	/* 1 = [shift+1:shift] (0=APLL 1=MPLL 2=VPLL),
			 * 0 = single bit (0=APLL 1=MPLL) */
	u16 gate_reg;	/* CLKGR0/CLKGR1 offset, 0xffff = no gate */
	u8 gate_bit;	/* gate bit (set = clock disabled) */
	u8 exact;	/* rate must divide exactly; may retarget the source */
};

#define NO_GATE 0xffff

/*
 * Leaf clocks U-Boot's drivers reference by the canonical binding ID.
 * Bit offsets are the exact vendor t10/clk.c cgu_clk_sel[] entries
 * (SSI/MSC/MAC src = single bit 31; ce/busy/stop =
 * 29/28/27 for all three). T10 has a single MSC (MSC0).
 */
static const struct t10_clk_desc t10_clks[T10_CLK_COUNT] = {
	[T10_CLK_SFC]  = { CPM_SSICDR, 29, 28, 27, 31, 0, CPM_CLKGR0, 20 },
	[T10_CLK_MSC0] = { CPM_MSC0CDR, 29, 28, 27, 31, 0, CPM_CLKGR0, 4 },
	/* GMAC feeds the RMII PHY 50 MHz ref: exact division required. */
	/*
	 * Kernel-consumed leaves with no U-Boot driver: modeled so the
	 * cgu node's assigned-clock-parents can pin their source muxes
	 * to the vendor contract (vendor cgu_clk_sel: VPU, ISP, CIM and
	 * MACPHY all from MPLL; T10 routes no VPLL to leaves and every
	 * mux is the single-bit geometry at bit 31). The 3.10 kernel
	 * computes leaf rates against whatever parent it inherits, so
	 * the inherited selector is the contract. Parents only.
	 */
	[T10_CLK_VPU]  = { CPM_VPUCDR, 29, 28, 27, 31, 0, NO_GATE, 0 },
	[T10_CLK_ISP]  = { CPM_ISPCDR, 29, 28, 27, 31, 0, NO_GATE, 0 },
	[T10_CLK_CIM]  = { CPM_CIMCDR, 29, 28, 27, 31, 0, NO_GATE, 0 },
	[T10_CLK_GMAC] = { CPM_MACCDR, 29, 28, 27, 31, 0, CPM_CLKGR1, 4, 1 },
	[T10_CLK_UART1] = { 0, 0, 0, 0, 0, 0, CPM_CLKGR0, 15 },
	[T10_CLK_OTG]  = { 0, 0, 0, 0, 0, 0, CPM_CLKGR0, 3 },
	[T10_CLK_TCU]  = { 0, 0, 0, 0, 0, 0, CPM_CLKGR0, 30 },
	[T10_CLK_OST]  = { 0, 0, 0, 0, 0, 0, CPM_CLKGR1, 11 },
};

struct t10_cgu_priv {
	void __iomem *base;
};

static u32 cpm_r(struct t10_cgu_priv *p, u32 off)
{
	return readl(p->base + off);
}

static void cpm_w(struct t10_cgu_priv *p, u32 off, u32 v)
{
	writel(v, p->base + off);
}

static ulong pll_rate(struct t10_cgu_priv *p, u32 reg)
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
static bool cdr_is_mpll(struct t10_cgu_priv *p, const struct t10_clk_desc *d)
{
	u32 v = cpm_r(p, d->cdr);

	if (d->src_2bit)
		return ((v >> d->src_shift) & 0x3) == 1;	/* 1 = MPLL */
	return (v >> d->src_shift) & 0x1;			/* 1 = MPLL */
}

static ulong t10_parent_rate(struct t10_cgu_priv *p,
			     const struct t10_clk_desc *d)
{
	if (cdr_is_mpll(p, d))
		return pll_rate(p, CPM_CPMPCR);		/* MPLL */
	return pll_rate(p, CPM_CPAPCR);			/* sclka ~ APLL */
}

static ulong t10_clk_get_rate(struct clk *clk)
{
	struct t10_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t10_clk_desc *d;

	switch (clk->id) {
	case T10_CLK_EXCLK:
		return EXT_RATE;
	case T10_CLK_RTCLK:
		return RTC_RATE;
	case T10_CLK_APLL:
		return pll_rate(p, CPM_CPAPCR);
	case T10_CLK_MPLL:
		return pll_rate(p, CPM_CPMPCR);
	case T10_CLK_UART1:
		return EXT_RATE;	/* T10 UART is clocked from EXT */
	}

	if (clk->id >= T10_CLK_COUNT)
		return -EINVAL;

	d = &t10_clks[clk->id];
	if (!d->cdr)
		return EXT_RATE;

	return t10_parent_rate(p, d) /
	       ((cpm_r(p, d->cdr) & CDR_DIV_MASK) + 1);
}

/*
 * Rate of a source-mux input, gated on the PLL lock bit so an absent
 * or idle PLL can never be selected as a clock source.
 */
static ulong t10_src_rate(struct t10_cgu_priv *p, u32 src)
{
	static const u16 pll_reg[] = {
		[SRC_SCLKA] = CPM_CPAPCR,
		[SRC_MPLL] = CPM_CPMPCR,
	};

	if (src >= ARRAY_SIZE(pll_reg) || !pll_reg[src])
		return 0;
	if (!(cpm_r(p, pll_reg[src]) & PLL_ON))
		return 0;

	return pll_rate(p, pll_reg[src]);
}

/* Program a CDR leaf: source mux + divider, one CE/BUSY sequence. */
static void t10_cdr_program(struct t10_cgu_priv *p,
			    const struct t10_clk_desc *d, u32 src, u32 div)
{
	u32 src_mask = (d->src_2bit ? 0x3u : 0x1u) << d->src_shift;
	u32 v = cpm_r(p, d->cdr);

	v &= ~(src_mask | BIT(d->stop) | BIT(d->busy) | CDR_DIV_MASK);
	v |= (src << d->src_shift) | BIT(d->ce) | (div - 1);
	cpm_w(p, d->cdr, v);

	while (cpm_r(p, d->cdr) & BIT(d->busy))
		;
	/* CE stays set - clearing it kills the clock on real silicon. */
}

static ulong t10_clk_set_rate(struct clk *clk, ulong rate)
{
	struct t10_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t10_clk_desc *d;
	ulong parent;
	u32 src, div;

	if (clk->id >= T10_CLK_COUNT)
		return -EINVAL;

	d = &t10_clks[clk->id];
	if (!d->cdr || !rate)
		return -ENOSYS;

	src = SRC_MPLL;
	parent = t10_src_rate(p, src);

	if (d->exact && (!parent || parent % rate)) {
		/* Retarget the mux to a locked PLL that divides exactly. */
		static const u8 alt[] = { SRC_SCLKA };
		int i;

		for (i = 0; i < ARRAY_SIZE(alt); i++) {
			ulong r;

			if (alt[i] == SRC_VPLL && !d->src_2bit)
				continue;	/* 1-bit mux: APLL/MPLL only */
			r = t10_src_rate(p, alt[i]);
			if (r >= rate && !(r % rate)) {
				src = alt[i];
				parent = r;
				break;
			}
		}
	}

	if (!parent)
		return -ENODEV;

	div = DIV_ROUND_CLOSEST(parent, rate);
	if (!div)
		div = 1;
	if (div > 256)
		div = 256;
	if (d->exact && parent % rate)
		dev_warn(clk->dev,
			 "clk %lu: no exact divider, %lu Hz off target %lu Hz\n",
			 clk->id, parent / div, rate);

	t10_cdr_program(p, d, src, div);

	return parent / div;
}

static int t10_clk_set_parent(struct clk *clk, struct clk *parent)
{
	struct t10_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t10_clk_desc *d;
	u32 src, div;

	if (clk->id >= T10_CLK_COUNT)
		return -EINVAL;

	d = &t10_clks[clk->id];
	if (!d->cdr)
		return -ENOSYS;

	switch (parent->id) {
	case T10_CLK_APLL:
		src = SRC_SCLKA;
		break;
	case T10_CLK_MPLL:
		src = SRC_MPLL;
		break;
	default:
		return -EINVAL;
	}

	if (src == SRC_VPLL && !d->src_2bit)
		return -EINVAL;	/* 1-bit mux: APLL/MPLL only */
	if (!t10_src_rate(p, src))
		return -ENODEV;

	/* Keep the current divider; the consumer re-rates afterwards. */
	div = (cpm_r(p, d->cdr) & CDR_DIV_MASK) + 1;
	t10_cdr_program(p, d, src, div);

	return 0;
}

static int t10_clk_gate(struct clk *clk, bool enable)
{
	struct t10_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t10_clk_desc *d;

	if (clk->id >= T10_CLK_COUNT)
		return -EINVAL;

	d = &t10_clks[clk->id];
	if (d->gate_reg == NO_GATE || !d->gate_reg)
		return 0;

	/* The CLKGR bit is set to DISABLE the clock. */
	if (enable)
		clrbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));
	else
		setbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));

	return 0;
}

static int t10_clk_enable(struct clk *clk)
{
	return t10_clk_gate(clk, true);
}

static int t10_clk_disable(struct clk *clk)
{
	return t10_clk_gate(clk, false);
}

static int t10_clk_of_xlate(struct clk *clk,
			    struct ofnode_phandle_args *args)
{
	if (args->args_count != 1)
		return -EINVAL;

	clk->id = args->args[0];
	return 0;
}

static const struct clk_ops t10_clk_ops = {
	.of_xlate = t10_clk_of_xlate,
	.get_rate = t10_clk_get_rate,
	.set_rate = t10_clk_set_rate,
	.set_parent = t10_clk_set_parent,
	.enable	  = t10_clk_enable,
	.disable  = t10_clk_disable,
};

static int t10_cgu_probe(struct udevice *dev)
{
	struct t10_cgu_priv *p = dev_get_priv(dev);

	p->base = (void __iomem *)T10_CPM_BASE;
	return 0;
}

static const struct udevice_id t10_cgu_ids[] = {
	{ .compatible = "ingenic,t10-cgu" },
	{ }
};

U_BOOT_DRIVER(ingenic_t10_cgu) = {
	.name		= "ingenic_t10_cgu",
	.id		= UCLASS_CLK,
	.of_match	= t10_cgu_ids,
	.probe		= t10_cgu_probe,
	.priv_auto	= sizeof(struct t10_cgu_priv),
	.ops		= &t10_clk_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
