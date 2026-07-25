// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 CGU (Clock/Power Manager) clock driver.
 *
 * The SPL brings up the PLLs and the CPU/DDR/bus dividers imperatively
 * (it is size-constrained and runs before DM); this driver runs in
 * U-Boot proper and models the leaf peripheral clocks the U-Boot
 * drivers actually consume: it recalculates the PLL rates from the CPM
 * registers and provides get_rate / set_rate / enable / disable for
 * SFC, MMC and the GMAC clock.
 *
 * T32 (PRJ007) is "T31-extended" but the CGU is NOT bit-compatible
 * with T31: the SFC clock is a dedicated divider (CPM_SFC0CDR, not the
 * shared SSICDR), every CDR uses ce/busy/stop = 29/28/27 (T31's SSI
 * used 28/27/26), and the CLKGR* gate bit map is different (SFC =
 * CLKGR0 bit 17, GMAC = CLKGR1 bit 0, ...). Register model and bit
 * positions are taken from the vendor U-Boot 2022.10 arch-PRJ cpm.h
 * and cgu_clk_sel[] table, not mirrored from the T31 driver.
 */

#include <clk-uclass.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <dt-bindings/clock/ingenic,t32-cgu.h>

#define T32_CLK_COUNT		(T32_CLK_VPU + 1)

/* CPM at physical 0x10000000, accessed through the uncached KSEG1 window. */
#define T32_CPM_BASE		0xb0000000

#define CPM_CPCCR		0x00
#define CPM_CPAPCR		0x10	/* APLL */
#define CPM_CPMPCR		0x14	/* MPLL */
#define CPM_CLKGR0		0x20
#define CPM_CLKGR1		0x28
#define CPM_HELIXCDR		0x30
#define CPM_MACCDR		0x54
#define CPM_SFC0CDR		0x58
#define CPM_CIMCDR		0x7c
#define CPM_ISPMCDR		0x80
#define CPM_MSC0CDR		0x68
#define CPM_MSC1CDR		0xa4
#define CPM_CPVPCR		0xe0	/* VPLL */

#define EXT_RATE		24000000UL
#define RTC_RATE		32768UL

/* CDR source field [31:30]: 0=APLL 1=MPLL 2=VPLL */
#define CDR_SRC_SHIFT		30
#define CDR_SRC_MASK		(3u << CDR_SRC_SHIFT)
#define CDR_SRC_SCLKA		0
#define CDR_SRC_MPLL		1
#define CDR_SRC_VPLL		2

/* CPxPCR PLL lock status (bit 3 = PLL stable). */
#define PLL_ON			BIT(3)
#define CDR_DIV_MASK		0xffu

struct t32_clk_desc {
	u16 cdr;	/* CPM CDR register offset, 0 = no divider */
	u8 ce;		/* clock-change-enable bit in cdr */
	u8 busy;	/* divider-busy bit in cdr */
	u8 stop;	/* clock-stop bit in cdr */
	u8 h_freq;	/* extra /2 bit position (NO_HFREQ = none) */
	u16 gate_reg;	/* CLKGR0/CLKGR1 offset, NO_GATE = no gate */
	u8 gate_bit;	/* gate bit (set = clock disabled) */
	u8 exact;	/* rate must divide exactly; may retarget the source */
};

#define NO_GATE  0xffff
#define NO_HFREQ 0xff

/*
 * Leaf clocks U-Boot's drivers reference by the canonical binding ID.
 * All T32 CDRs use ce/busy/stop = 29/28/27. MSC0 and MSC1 carry an
 * additional H_FREQ /2 bit at position 20 of the CDR, used to reach
 * the sub-MHz card clock the SDHCI core wants for SD init. Gate bits
 * (set = disabled): CLKGR0 - SFC0 17, MSC0 3, MSC1 4, OTG 2, UART0
 * 11, UART1 12, TCU 26; CLKGR1 - GMAC 0, OST 7.
 */
static const struct t32_clk_desc t32_clks[T32_CLK_COUNT] = {
	/*
	 * Kernel-consumed leaves with no U-Boot driver: modeled so the
	 * cgu node's assigned-clock-parents can pin their source muxes
	 * to the vendor contract, measured from the shipping stock
	 * loader on T32LQ silicon (2013.07-H20250211a register dump):
	 * HELIX (VPU), ISPM and CIM all parked on MPLL. Unlike the
	 * other XBurst1 parts, T32 stock parks CIM on MPLL, not VPLL.
	 * Parents only; rates stay the OS's business.
	 */
	[T32_CLK_VPU]  = { CPM_HELIXCDR, 29, 28, 27, NO_HFREQ, NO_GATE, 0 },
	[T32_CLK_ISP]  = { CPM_ISPMCDR, 29, 28, 27, NO_HFREQ, NO_GATE, 0 },
	[T32_CLK_CIM]  = { CPM_CIMCDR, 29, 28, 27, NO_HFREQ, NO_GATE, 0 },
	[T32_CLK_SFC]   = { CPM_SFC0CDR, 29, 28, 27, NO_HFREQ, CPM_CLKGR0, 17 },
	[T32_CLK_MSC0]  = { CPM_MSC0CDR, 29, 28, 27, 20,       CPM_CLKGR0, 3 },
	[T32_CLK_MSC1]  = { CPM_MSC1CDR, 29, 28, 27, 20,       CPM_CLKGR0, 4 },
	/* GMAC feeds the RMII PHY 50 MHz ref: exact division required. */
	[T32_CLK_GMAC]  = { CPM_MACCDR,  29, 28, 27, NO_HFREQ, CPM_CLKGR1, 0, 1 },
	[T32_CLK_UART0] = { 0, 0, 0, 0, NO_HFREQ, CPM_CLKGR0, 11 },
	[T32_CLK_UART1] = { 0, 0, 0, 0, NO_HFREQ, CPM_CLKGR0, 12 },
	[T32_CLK_OTG]   = { 0, 0, 0, 0, NO_HFREQ, CPM_CLKGR0, 2 },
	[T32_CLK_TCU]   = { 0, 0, 0, 0, NO_HFREQ, CPM_CLKGR0, 26 },
	[T32_CLK_OST]   = { 0, 0, 0, 0, NO_HFREQ, CPM_CLKGR1, 7 },
};

struct t32_cgu_priv {
	void __iomem *base;
};

static u32 cpm_r(struct t32_cgu_priv *p, u32 off)
{
	return readl(p->base + off);
}

static void cpm_w(struct t32_cgu_priv *p, u32 off, u32 v)
{
	writel(v, p->base + off);
}

static ulong pll_rate(struct t32_cgu_priv *p, u32 reg)
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

static ulong t32_parent_rate(struct t32_cgu_priv *p, u32 cdr)
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

static ulong t32_clk_get_rate(struct clk *clk)
{
	struct t32_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t32_clk_desc *d;
	u32 cdr_val;
	ulong rate;

	switch (clk->id) {
	case T32_CLK_EXCLK:
		return EXT_RATE;
	case T32_CLK_RTCLK:
		return RTC_RATE;
	case T32_CLK_APLL:
		return pll_rate(p, CPM_CPAPCR);
	case T32_CLK_MPLL:
		return pll_rate(p, CPM_CPMPCR);
	case T32_CLK_VPLL:
		return pll_rate(p, CPM_CPVPCR);
	case T32_CLK_UART0:
	case T32_CLK_UART1:
		return EXT_RATE;	/* T32 UART is clocked from EXT */
	}

	if (clk->id >= T32_CLK_COUNT)
		return -EINVAL;

	d = &t32_clks[clk->id];
	if (!d->cdr)
		return EXT_RATE;

	cdr_val = cpm_r(p, d->cdr);
	rate = t32_parent_rate(p, d->cdr) / ((cdr_val & CDR_DIV_MASK) + 1);
	if (d->h_freq != NO_HFREQ && (cdr_val & BIT(d->h_freq)))
		rate /= 2;
	return rate;
}

/*
 * Rate of a CDR source-mux input, gated on the PLL lock bit so an
 * absent or idle PLL can never be selected as a clock source.
 */
static ulong t32_src_rate(struct t32_cgu_priv *p, u32 src)
{
	static const u16 pll_reg[] = {
		[CDR_SRC_SCLKA] = CPM_CPAPCR,
		[CDR_SRC_MPLL] = CPM_CPMPCR,
		[CDR_SRC_VPLL] = CPM_CPVPCR,
	};

	if (src >= ARRAY_SIZE(pll_reg))
		return 0;
	if (!(cpm_r(p, pll_reg[src]) & PLL_ON))
		return 0;

	return pll_rate(p, pll_reg[src]);
}

static ulong t32_clk_set_rate(struct clk *clk, ulong rate)
{
	struct t32_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t32_clk_desc *d;
	ulong parent;
	u32 src, div, v;
	bool use_hfreq = false;

	if (clk->id >= T32_CLK_COUNT)
		return -EINVAL;

	d = &t32_clks[clk->id];
	if (!d->cdr || !rate)
		return -ENOSYS;

	src = CDR_SRC_MPLL;
	parent = t32_src_rate(p, src);

	if (d->exact && (!parent || parent % rate)) {
		/* Retarget the mux to a locked PLL that divides exactly. */
		static const u8 alt[] = { CDR_SRC_VPLL, CDR_SRC_SCLKA };
		int i;

		for (i = 0; i < ARRAY_SIZE(alt); i++) {
			ulong r = t32_src_rate(p, alt[i]);

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

	/*
	 * The 8-bit CDR field tops out at 256. When the requested rate
	 * needs a deeper divisor and the clock supports the H_FREQ /2
	 * bit (MSC0/MSC1), use it to halve the effective rate after
	 * the divider; this extends the reachable range to 512.
	 */
	if (div > 256 && d->h_freq != NO_HFREQ) {
		div = DIV_ROUND_CLOSEST(parent, rate * 2);
		if (!div)
			div = 1;
		use_hfreq = true;
	}
	if (div > 256)
		div = 256;
	if (d->exact && parent % rate)
		dev_warn(clk->dev,
			 "clk %lu: no exact divider, %lu Hz off target %lu Hz\n",
			 clk->id, parent / div, rate);

	v = cpm_r(p, d->cdr);
	v &= ~(CDR_SRC_MASK | BIT(d->stop) | BIT(d->busy) | CDR_DIV_MASK);
	if (d->h_freq != NO_HFREQ)
		v &= ~BIT(d->h_freq);
	v |= (src << CDR_SRC_SHIFT) | BIT(d->ce) | (div - 1);
	if (use_hfreq)
		v |= BIT(d->h_freq);
	cpm_w(p, d->cdr, v);

	while (cpm_r(p, d->cdr) & BIT(d->busy))
		;
	/* CE stays set - clearing it kills the clock on real silicon. */

	return use_hfreq ? parent / div / 2 : parent / div;
}

static int t32_clk_set_parent(struct clk *clk, struct clk *parent)
{
	struct t32_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t32_clk_desc *d;
	u32 src, div, v;

	if (clk->id >= T32_CLK_COUNT)
		return -EINVAL;

	d = &t32_clks[clk->id];
	if (!d->cdr)
		return -ENOSYS;

	switch (parent->id) {
	case T32_CLK_APLL:
		src = CDR_SRC_SCLKA;
		break;
	case T32_CLK_MPLL:
		src = CDR_SRC_MPLL;
		break;
	case T32_CLK_VPLL:
		src = CDR_SRC_VPLL;
		break;
	default:
		return -EINVAL;
	}

	if (!t32_src_rate(p, src))
		return -ENODEV;

	/* Keep the current divider/hfreq; the consumer re-rates afterwards. */
	v = cpm_r(p, d->cdr);
	div = (v & CDR_DIV_MASK) + 1;
	v &= ~(CDR_SRC_MASK | BIT(d->stop) | BIT(d->busy) | CDR_DIV_MASK);
	v |= (src << CDR_SRC_SHIFT) | BIT(d->ce) | (div - 1);
	cpm_w(p, d->cdr, v);

	while (cpm_r(p, d->cdr) & BIT(d->busy))
		;

	return 0;
}

static int t32_clk_gate(struct clk *clk, bool enable)
{
	struct t32_cgu_priv *p = dev_get_priv(clk->dev);
	const struct t32_clk_desc *d;

	if (clk->id >= T32_CLK_COUNT)
		return -EINVAL;

	d = &t32_clks[clk->id];
	if (d->gate_reg == NO_GATE || !d->gate_reg)
		return 0;

	/* The CLKGR bit is set to DISABLE the clock. */
	if (enable)
		clrbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));
	else
		setbits_le32(p->base + d->gate_reg, BIT(d->gate_bit));

	return 0;
}

static int t32_clk_enable(struct clk *clk)
{
	return t32_clk_gate(clk, true);
}

static int t32_clk_disable(struct clk *clk)
{
	return t32_clk_gate(clk, false);
}

static int t32_clk_of_xlate(struct clk *clk,
			    struct ofnode_phandle_args *args)
{
	if (args->args_count != 1)
		return -EINVAL;

	clk->id = args->args[0];
	return 0;
}

static const struct clk_ops t32_clk_ops = {
	.of_xlate = t32_clk_of_xlate,
	.get_rate = t32_clk_get_rate,
	.set_rate = t32_clk_set_rate,
	.set_parent = t32_clk_set_parent,
	.enable	  = t32_clk_enable,
	.disable  = t32_clk_disable,
};

static int t32_cgu_probe(struct udevice *dev)
{
	struct t32_cgu_priv *p = dev_get_priv(dev);

	p->base = (void __iomem *)T32_CPM_BASE;
	return 0;
}

static const struct udevice_id t32_cgu_ids[] = {
	{ .compatible = "ingenic,t32-cgu" },
	{ }
};

U_BOOT_DRIVER(ingenic_t32_cgu) = {
	.name		= "ingenic_t32_cgu",
	.id		= UCLASS_CLK,
	.of_match	= t32_cgu_ids,
	.probe		= t32_cgu_probe,
	.priv_auto	= sizeof(struct t32_cgu_priv),
	.ops		= &t32_clk_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
