// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 on-chip audio codec - playback.
 *
 * The T32 internal codec is a different silicon block from the
 * "jz_t10" family used on T10/T20/T21/T23/T30/T31/C100: codec base
 * 0x10021000 (vendor CODEC_IOBASE), 8-bit registers accessed via
 * word reads/writes, register set CDGR/CDDACR{1..6}/CDADCR{1..5}/
 * CAGR{1..3}/CADACR{1..4}/CAADCR{1..4}/CAGCR{1..14}. The B-block
 * jz_t10 setup does not apply here, so this is a separate driver.
 *
 * Faithful port of the playback path in the vendor SDK 5.15.170
 * OSS3 driver (audio/oss3/inner_codecs/PRJ007/codec.c). The lab
 * T32LQ board has a class-D amp hardwired to LINE/HP-OUT (no
 * external enable line), so no spk-gpio handling is needed - the
 * codec itself is the only thing to bring up. The DT may still
 * carry ingenic,spk-gpio for boards that do gate the amp, in which
 * case it is parsed and driven raw last (anti-pop).
 *
 * The msleep() values in the vendor sequence are analog charge-pump
 * settling requirements; they are preserved as mdelay() here.
 */

#define LOG_CATEGORY UCLASS_AUDIO_CODEC

#include <audio_codec.h>
#include <dm.h>
#include <log.h>
#include <asm/io.h>
#include <linux/delay.h>

/* T32 codec register offsets (vendor inner_codecs/PRJ007/codec.h). */
#define T32_CODEC_CDGR		0x00
#define T32_CODEC_CDDACR1	0x04
#define T32_CODEC_CDDACR2	0x08
#define T32_CODEC_CDDACR6	0x18
#define T32_CODEC_CAGR1		0x80
#define T32_CODEC_CAGR2		0x84
#define T32_CODEC_CADACR1	0xa0
#define T32_CODEC_CADACR2	0xa4
#define T32_CODEC_CADACR4	0xac
#define T32_CODEC_CAGCR10	0x128

/* CPM AIC gate - the codec island shares it (T32: CLKGR0 bit 8). */
#define CPM_BASE		0xb0000000
#define CPM_CLKGR0		0x20
#define T32_CPM_CLKGR0_AIC	BIT(8)

/*
 * Optional speaker-amp enable GPIO. Same raw-drive scheme as the
 * jz_t10_codec driver; T32 uses the 0x1000 GPIO port stride. The
 * lab T32LQ board does not need this (amp is hardwired); honour it
 * only if the DT supplies ingenic,spk-gpio.
 */
#define GPIO_BASE_PHYS		0xb0010000u
#define T32_GPIO_STRIDE		0x1000u
#define G_PXINTC		0x18	/* INT clear  -> not interrupt */
#define G_PXMSKS		0x24	/* MSK set    -> GPIO (not device) */
#define G_PXPAT1C		0x38	/* PAT1 clear -> output */
#define G_PXPAT0S		0x44	/* PAT0 set   -> drive high */
#define G_PXPAT0C		0x48	/* PAT0 clear -> drive low  */

struct t32_codec_priv {
	void __iomem *base;
	bool		has_spk;
	bool		spk_active_low;
	u8		spk_port;	/* 0=A 1=B 2=C 3=D */
	u8		spk_pin;	/* 0..31 */
	u8		hp_gain;	/* CADACR4[0:4], default 0x18 */
};

static void t32_codec_spk_drive(struct t32_codec_priv *p, bool on)
{
	void __iomem *gpx;
	u32 bit;
	bool high;

	if (!p->has_spk)
		return;

	gpx  = (void __iomem *)(GPIO_BASE_PHYS +
				p->spk_port * T32_GPIO_STRIDE);
	bit  = BIT(p->spk_pin);
	high = on ^ p->spk_active_low;

	writel(bit, gpx + G_PXINTC);
	writel(bit, gpx + G_PXMSKS);
	writel(bit, gpx + G_PXPAT1C);
	writel(bit, gpx + (high ? G_PXPAT0S : G_PXPAT0C));
}

/* Vendor codec_reg_set: bitfield read-modify-write into [end:start]. */
static void codec_set(struct t32_codec_priv *p, u32 reg, int start,
		      int end, u32 val)
{
	u32 mask = ((1u << (end - start + 1)) - 1) << start;
	u32 v = readl(p->base + reg);

	v = (v & ~mask) | ((val << start) & mask);
	writel(v, p->base + reg);
}

/* Map sample rate -> CAGCR10[2:0] fs select field. */
static u32 t32_codec_fs(int rate)
{
	static const int hz[8] = { 8000, 12000, 16000, 24000,
				   32000, 44100, 48000, 96000 };
	static const u32 fs[8] = { 7, 6, 5, 4, 3, 2, 1, 0 };
	int i;

	for (i = 0; i < 8; i++)
		if (hz[i] == rate)
			return fs[i];
	return 2;	/* default 44100 */
}

/* Vendor codec_init() - bring up VREF, charge pump, DAC bias. */
static void t32_codec_init(struct t32_codec_priv *p)
{
	u8 value = 0;
	int i;

	/* step1. Power up the digital section + soft-reset. */
	codec_set(p, T32_CODEC_CDGR, 0, 7, 0xc0);
	mdelay(1);
	codec_set(p, T32_CODEC_CDGR, 0, 7, 0xf1);
	mdelay(1);

	/* step2. POP_CTRL_DACL = 01: anti-pop output DC for DAC L. */
	codec_set(p, T32_CODEC_CADACR1, 5, 6, 0x1);
	mdelay(1);

	/* step3. SEL_VREF = 0x01 - VREF pre-charge select. */
	codec_set(p, T32_CODEC_CAGR2, 0, 7, 0x01);
	mdelay(1);

	/* step4. EN_VREF = 1 - enable reference voltage. */
	codec_set(p, T32_CODEC_CAGR1, 0, 0, 0x1);
	mdelay(1);

	/* step5. SEL_VREF ramp: 8 steps, doubling the bias each round. */
	for (i = 0; i < 8; i++) {
		value = (value << 1) | 0x01;
		codec_set(p, T32_CODEC_CAGR2, 0, 7, value);
		mdelay(10);
	}

	/* step6. Settle to minimum bias for low power. */
	codec_set(p, T32_CODEC_CAGR2, 0, 7, 0x02);
	mdelay(10);

	/* Finalize VREF / DAC bias trim. */
	codec_set(p, T32_CODEC_CAGR1,    1, 1, 0x1);
	codec_set(p, T32_CODEC_CADACR1,  2, 2, 0x1);
	codec_set(p, T32_CODEC_CADACR1,  5, 6, 0x2);
}

/* Vendor codec_enable_playback() - DAC + HPDRV up, route, set gain. */
static void t32_codec_enable_playback(struct t32_codec_priv *p)
{
	/* Choose DAC I2S interface mode. */
	codec_set(p, T32_CODEC_CDDACR1, 2, 3, 0x2);
	/* Choose DAC I2S master mode (the codec drives BITCLK/LRCK). */
	codec_set(p, T32_CODEC_CDDACR2, 4, 5, 0x3);

	/* HPDRV enable + end-of-init. */
	codec_set(p, T32_CODEC_CADACR2, 0, 0, 0x1);
	codec_set(p, T32_CODEC_CADACR2, 2, 2, 0x1);

	/* DAC vref + clock + module enable, then end-of-init. */
	codec_set(p, T32_CODEC_CADACR1, 3, 3, 0x1);
	codec_set(p, T32_CODEC_CADACR1, 1, 1, 0x1);
	codec_set(p, T32_CODEC_CADACR1, 0, 0, 0x1);
	mdelay(1);
	codec_set(p, T32_CODEC_CADACR1, 4, 4, 0x1);

	/* Unmute HPDRV. */
	codec_set(p, T32_CODEC_CADACR2, 5, 5, 0x1);

	/* HPDRV output gain (vendor default 0x18, ~mid-scale). */
	codec_set(p, T32_CODEC_CADACR4, 0, 4, p->hp_gain);
}

static int t32_codec_set_params(struct udevice *dev, int interface,
				int rate, int mclk_freq,
				int bits_per_sample, uint channels)
{
	struct t32_codec_priv *p = dev_get_priv(dev);

	/* Codec island shares the AIC gate; ungate defensively in case
	 * the codec is probed before the i2s device. */
	clrbits_le32((void __iomem *)(CPM_BASE + CPM_CLKGR0),
		     T32_CPM_CLKGR0_AIC);

	t32_codec_init(p);
	t32_codec_enable_playback(p);

	/* Sample-rate select last (CAGCR10[2:0]). */
	codec_set(p, T32_CODEC_CAGCR10, 0, 2, t32_codec_fs(rate));
	mdelay(1);

	/* Amp on LAST (anti-pop); no-op on the lab T32LQ (hardwired amp). */
	t32_codec_spk_drive(p, true);

	return 0;
}

static int t32_codec_probe(struct udevice *dev)
{
	struct t32_codec_priv *p = dev_get_priv(dev);
	struct ofnode_phandle_args args;
	fdt_addr_t addr;
	u32 v;

	addr = dev_read_addr(dev);
	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;
	p->base = map_physmem(addr, 0x140, MAP_NOCACHE);

	/* HPDRV gain: 0..0x1f (CADACR4[0:4]). DT-overridable, vendor 0x18. */
	p->hp_gain = dev_read_u32_default(dev, "ingenic,replay-volume",
					  0x18) & 0x1f;

	/* Optional speaker-amp enable GPIO. */
	if (!dev_read_phandle_with_args(dev, "ingenic,spk-gpio",
					"#gpio-cells", 0, 0, &args) &&
	    !ofnode_read_u32(args.node, "reg", &v) &&
	    v < 8 && args.args[0] < 32) {
		p->spk_port	   = (u8)v;
		p->spk_pin	   = (u8)args.args[0];
		p->spk_active_low  = !!(args.args[1] & 1);
		p->has_spk	   = true;
	}

	return 0;
}

static const struct audio_codec_ops t32_codec_ops = {
	.set_params	= t32_codec_set_params,
};

static const struct udevice_id t32_codec_ids[] = {
	{ .compatible = "ingenic,t32-codec" },
	{ }
};

U_BOOT_DRIVER(t32_codec) = {
	.name		= "t32_codec",
	.id		= UCLASS_AUDIO_CODEC,
	.of_match	= t32_codec_ids,
	.probe		= t32_codec_probe,
	.ops		= &t32_codec_ops,
	.priv_auto	= sizeof(struct t32_codec_priv),
};
