// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 (XBurst2) audio - self-contained driver for the on-chip codec
 * + AIC, a faithful port of the vendor SDK ISVP-A1-1.7.0-20250407 kernel
 * driver (opensource/drivers/audio: inner_codecs/A1/codec.c + host/A1/
 * audio_aic.c, driver version H20230517a).
 *
 * Same shape as the T41 driver (drivers/sound/t41_audio.c) - three DM
 * drivers (codec / aic-i2s / sound) so U-Boot's sound uclass + `sound`
 * command work unchanged, with the sound driver sequencing the codec
 * (the I2S master) and the AIC (the slave) in the vendor's order. The A1
 * codec is its own register block (word-spaced "indexed" regs at
 * 0xb0022000), distinct from T41's E-block; the AIC is T41-like
 * (RMASTER/TMASTER master select) but at A1's own CGU/gate offsets.
 *
 * All A1-specific addresses below were confirmed against a live /dev/mem
 * dump of a thingino A1 playing a tone (see project_uboot_a1_audio memory):
 * codec @ 0xb0022000 (NOT 0xb0021000), I2ST0CDR @ CGU+0xa0 = 0x220021b1,
 * AIC gate CLKGR0[30], DMAC gate CLKGR1[3], AIC0 @ 0xb0020000.
 */

#define LOG_CATEGORY UCLASS_SOUND

#include <cpu_func.h>
#include <audio_codec.h>
#include <dm.h>
#include <i2s.h>
#include <log.h>
#include <sound.h>
#include <asm/io.h>
#include <linux/delay.h>

/* ---- register blocks (KSEG1 uncached) ---- */
#define CGU_BASE	0xb0000000
#define AIC_BASE	0xb0020000
#define PDMA_BASE	0xb3420000

/* CGU - A1 map (differs from T41: AIC gate bit 30, I2ST0CDR at 0xa0). */
#define CGU_CLKGR0	0x30
#define CGU_CLKGR1	0x38
#define CGU_I2STCDR	0xa0		/* I2S TX clock divider */
#define CLKGR0_AIC	BIT(30)
#define CLKGR1_DMAC	BIT(3)		/* PDMA controller gate */
#define I2SCDR_VAL	0x220021b1	/* SCLKA/APLL, 256*16000 (vendor live) */
#define I2SCDR_CE	BIT(29)

/* AIC */
#define AICFR		0x00
#define AICCR		0x04
#define I2SCR		0x10
#define AICSR		0x14
#define I2SDIV		0x30
#define AICDR		0x34
#define AICDR_PHYS	0x10020034
#define AICFR_ENB	BIT(0)
#define AICFR_RMASTER	BIT(1)
#define AICFR_TMASTER	BIT(2)
#define AICFR_RST	BIT(3)
#define AICFR_AUSEL	BIT(4)		/* 1 = I2S */
#define AICFR_ICDC	BIT(5)		/* internal codec */
#define AICFR_LSMP	BIT(6)		/* repeat last sample on underrun */
#define AICFR_RFIFOS	BIT(13)
#define AICFR_TFIFOS	BIT(14)
#define AICFR_TFTH_SH	16
#define AICCR_ERPL	BIT(1)
#define AICCR_TFLUSH	BIT(8)
#define AICCR_ASVTSU	BIT(9)
#define AICCR_ENDSW	BIT(10)
#define AICCR_M2S	BIT(11)		/* mono -> stereo expand */
#define AICCR_TDMS	BIT(14)		/* TX fed by DMA */
#define AICCR_OSS_SH	19
#define AICCR_OSS_MSK	(0x7u << AICCR_OSS_SH)
#define AICCR_CHAN_SH	24
#define AICCR_CHAN_MSK	(0x7u << AICCR_CHAN_SH)
#define AICCR_CLEAR	(AICCR_TDMS | AICCR_ERPL | BIT(0) | BIT(3) | BIT(4) | \
			 BIT(5) | BIT(6))
#define AICSR_TUR	BIT(5)

/* PDMA (X2000/A1/T41) */
#define PDMA_DMAC	0x1000
#define PDMA_DMAC_DMAE	BIT(0)
#define PDMA_CH_DSA	0x00
#define PDMA_CH_DTA	0x04
#define PDMA_CH_DTC	0x08
#define PDMA_CH_DRT	0x0c
#define PDMA_CH_DCS	0x10
#define PDMA_CH_DCM	0x14
#define PDMA_DCS_NDES	BIT(31)
#define PDMA_DCS_CTE	BIT(0)
#define PDMA_DCS_TT	BIT(3)
#define PDMA_DCS_HLT	BIT(2)
#define PDMA_DCS_AR	BIT(4)
#define PDMA_DRT_AIC_TX	0x6
#define PDMA_DCM_TX	(BIT(23) | (2u << 14 | 2u << 12) | (7u << 8) | (8u << 16))

/* ---- A1 codec register offsets (base 0xb0022000, word-spaced idx<<2) ---- */
#define C_GLB_00	(0x00 << 2)	/* 0x000 */
#define C_MODE_CTL_03	(0x03 << 2)	/* 0x00c */
#define C_DAC_CTL1_04	(0x04 << 2)	/* 0x010 */
#define C_DAC_CTL2_05	(0x05 << 2)	/* 0x014 */
#define C_DAC_POWER_20	(0x20 << 2)	/* 0x080 */
#define C_PRECHARGE_21	(0x21 << 2)	/* 0x084 */
#define C_ADC_ANL_22	(0x22 << 2)	/* 0x088 */
#define C_DAC_CLK_29	(0x29 << 2)	/* 0x0a4 */
#define C_HPOUTL_GAIN_2a (0x2a << 2)	/* 0x0a8 */
#define C_HPOUTR_GAIN_2b (0x2b << 2)	/* 0x0ac */
#define C_HPOUT_2c	(0x2c << 2)	/* 0x0b0 */
#define C_HPOUT_CTL_2d	(0x2d << 2)	/* 0x0b4 */
#define C_DAC_POP_2e	(0x2e << 2)	/* 0x0b8 */
#define C_SAMPLE_RATE_44 (0x44 << 2)	/* 0x110 */

struct a1_codec_priv {
	void __iomem *base;
	u8 volume;
};

struct a1_aic_priv {
	void __iomem *base;
};

/*
 * Vendor codec_reg_set: bitfield RMW with a settling delay BEFORE each
 * write (msleep(20) for the precharge register, msleep(1) otherwise) -
 * these are real analog-settling requirements, kept as mdelay().
 */
static void cset(void __iomem *b, u32 reg, int s, int e, u32 val)
{
	u32 mask = (((1u << (e - s + 1)) - 1) << s);
	u32 v;

	mdelay(reg == C_PRECHARGE_21 ? 20 : 1);
	v = readl(b + reg);
	v = (v & ~mask) | ((val << s) & mask);
	writel(v, b + reg);
}

static inline void rmw(void __iomem *b, u32 off, u32 clr, u32 set)
{
	writel((readl(b + off) & ~clr) | set, b + off);
}

/* ============================ codec ============================ */

/* vendor codec_init(): reset, DC bias, charge-pump precharge ramp. ONCE. */
static void a1_codec_init(void __iomem *c)
{
	u32 value = 0;
	int i;

	cset(c, C_GLB_00, 0, 1, 0x0);
	cset(c, C_GLB_00, 0, 1, 0x3);		/* power digital + un-reset */
	cset(c, C_DAC_CLK_29, 4, 5, 0x1);	/* DAC output DC voltage */
	cset(c, C_DAC_POP_2e, 4, 5, 0x1);
	cset(c, C_PRECHARGE_21, 0, 7, 0x1);
	cset(c, C_ADC_ANL_22, 5, 5, 0x1);	/* reference voltage */
	for (i = 0; i <= 7; i++) {
		value |= value << 1 | 0x01;
		cset(c, C_PRECHARGE_21, 0, 7, value);
	}
	cset(c, C_PRECHARGE_21, 0, 7, 0x02);	/* min charge current */
}

/* vendor codec_enable_stereo_playback(): DAC I2S master + analog chain. */
static void a1_codec_enable_playback(struct udevice *dev)
{
	struct a1_codec_priv *p = dev_get_priv(dev);
	void __iomem *c = p->base;

	cset(c, C_MODE_CTL_03, 6, 7, 0x3);	/* DAC I2S MASTER */
	cset(c, C_DAC_CTL1_04, 3, 4, 0x2);
	cset(c, C_DAC_CTL1_04, 0, 2, 0x0);
	cset(c, C_DAC_CTL1_04, 7, 7, 0x0);
	cset(c, C_DAC_CTL1_04, 5, 6, 0x0);	/* DAC valid data length = 16-bit
						 * (vendor set_datatype; without it
						 * the codec mis-decodes -> static) */
	cset(c, C_DAC_CTL2_05, 0, 7, 0x0e);
	cset(c, C_DAC_POWER_20, 0, 3, 0x05);	/* DAC bias current */
	mdelay(10);
	/*
	 * Left (mono) DAC analog chain - vendor codec_enable_left_playback.
	 * The AIC M2S expands mono->stereo on the I2S bus; only the LEFT DAC is
	 * brought up. Matches the live thingino playback dump exactly - touching
	 * the right-channel mirror regs (HPOUT_2c / HPOUT_CTL_2d / DAC_POP_2e)
	 * as the stereo path does produced static.
	 */
	cset(c, C_DAC_CLK_29, 7, 7, 1);		/* 1. current source */
	cset(c, C_DAC_CLK_29, 6, 6, 1);		/* 2. ref voltage buffer */
	cset(c, C_DAC_CLK_29, 4, 5, 2);		/* 3. ref voltage */
	cset(c, C_HPOUTL_GAIN_2a, 4, 4, 1);	/* 4. HPDRV enable */
	cset(c, C_HPOUTL_GAIN_2a, 5, 5, 1);	/* 5. HPDRV end-init */
	cset(c, C_DAC_CLK_29, 3, 3, 1);		/* 6. ref voltage */
	cset(c, C_DAC_CLK_29, 2, 2, 1);		/* 7. DAC clock */
	cset(c, C_DAC_CLK_29, 1, 1, 1);		/* 8. DAC module */
	cset(c, C_DAC_CLK_29, 0, 0, 1);		/* 9. end-init DAC */
	cset(c, C_HPOUTL_GAIN_2a, 6, 6, 1);	/* 10. unmute DRV */
	cset(c, C_HPOUTR_GAIN_2b, 0, 4, p->volume);	/* 11. HP gain (left) */
	mdelay(20);
}

static void a1_codec_disable_playback(struct udevice *dev)
{
	struct a1_codec_priv *p = dev_get_priv(dev);
	void __iomem *c = p->base;

	cset(c, C_HPOUTR_GAIN_2b, 0, 4, 0x0);	/* mute left */
	cset(c, C_HPOUTL_GAIN_2a, 6, 6, 0x0);
}

static int a1_codec_set_params(struct udevice *dev, int interface, int rate,
			       int mclk_freq, int bits_per_sample, uint channels)
{
	struct a1_codec_priv *p = dev_get_priv(dev);

	a1_codec_init(p->base);
	/* The vendor does NOT set the codec sample-rate register for playback
	 * (codec_set_sample_rate is commented out in set_datatype); the codec,
	 * as I2S master, derives fs from the I2S clock. The live thingino dump
	 * confirms SAMPLE_RATE_44 stays 0 - forcing it caused static. */
	return 0;
}

static int a1_codec_probe(struct udevice *dev)
{
	struct a1_codec_priv *p = dev_get_priv(dev);
	fdt_addr_t addr = dev_read_addr(dev);

	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;
	p->base = map_physmem(addr, 0x140, MAP_NOCACHE);
	p->volume = dev_read_u32_default(dev, "ingenic,replay-volume", 0x1f) & 0x1f;
	return 0;
}

static const struct audio_codec_ops a1_codec_ops = {
	.set_params = a1_codec_set_params,
};

static const struct udevice_id a1_codec_ids[] = {
	{ .compatible = "ingenic,a1-codec" },
	{ }
};

U_BOOT_DRIVER(a1_codec) = {
	.name		= "a1_codec",
	.id		= UCLASS_AUDIO_CODEC,
	.of_match	= a1_codec_ids,
	.probe		= a1_codec_probe,
	.ops		= &a1_codec_ops,
	.priv_auto	= sizeof(struct a1_codec_priv),
};

/* ============================ AIC ============================ */

static void a1_aic_clk_init(void)
{
	void __iomem *cgu = (void __iomem *)CGU_BASE;

	clrbits_le32(cgu + CGU_CLKGR0, CLKGR0_AIC);
	clrbits_le32(cgu + CGU_CLKGR1, CLKGR1_DMAC);
	/* Latch the fractional divider cleanly (CE 0->1 edge). */
	writel(I2SCDR_VAL & ~I2SCDR_CE, cgu + CGU_I2STCDR);
	writel(I2SCDR_VAL, cgu + CGU_I2STCDR);
	/* Enable the PDMA engine. */
	writel(PDMA_DMAC_DMAE, (void __iomem *)(PDMA_BASE + PDMA_DMAC));
}

/* Configure the AIC for the internal codec - NOT enabled (see a1_aic_enable).
 * Mirrors the vendor audio_aic.c live config (AICFR=0x07100071,
 * AICCR=0x000c4802, AICCR1=AICCR2=0, I2SCR=0, I2SDIV=0x40004). */
static void a1_aic_init(void __iomem *b)
{
	u32 tmo = 100000;

	rmw(b, AICFR, AICFR_ENB, 0);
	rmw(b, AICFR, 0, AICFR_RST);
	while ((readl(b + AICFR) & (AICFR_RFIFOS | AICFR_TFIFOS)) && --tmo)
		;
	rmw(b, AICFR, AICFR_RST, 0);

	rmw(b, AICFR, 0, AICFR_AUSEL);			/* I2S unit */
	rmw(b, AICFR, 0, AICFR_ICDC);			/* internal codec */
	rmw(b, AICFR, AICFR_RMASTER | AICFR_TMASTER, 0);/* AIC = I2S slave */

	/* 16-bit, mono + mono->stereo expand. */
	rmw(b, AICCR, AICCR_OSS_MSK, 1u << AICCR_OSS_SH);
	rmw(b, AICCR, AICCR_ENDSW | AICCR_ASVTSU, 0);
	rmw(b, AICCR, AICCR_CHAN_MSK, 0);		/* 1 channel */
	rmw(b, AICCR, 0, AICCR_M2S);			/* mono -> stereo */

	rmw(b, AICCR, AICCR_CLEAR, 0);			/* no replay yet */
	rmw(b, AICSR, AICSR_TUR, 0);
	rmw(b, AICFR, 0x1fu << AICFR_TFTH_SH, 16u << AICFR_TFTH_SH);

	writel(0, b + I2SCR);				/* A1 I2SCR = 0 */
	writel(0x40004, b + I2SDIV);
	rmw(b, AICFR, 0, AICFR_LSMP);			/* repeat last on underrun */
	rmw(b, AICCR, 0, AICCR_TDMS);			/* TX fed by DMA */
}

/* Enable the AIC unit - the AIC slave comes up before the codec masters
 * BITCLK; it just waits for the clock (proven order on T41). */
int a1_aic_enable(struct udevice *i2s)
{
	struct a1_aic_priv *a = dev_get_priv(i2s);

	rmw(a->base, AICFR, 0, AICFR_ENB);
	return 0;
}

/*
 * The caller's PCM is bounced through a static staging buffer: feeding the
 * sound subsystem's heap buffer to the PDMA directly produces static on A1,
 * while a static in-driver buffer through this identical path plays cleanly -
 * U-Boot's malloc region isn't DMA-coherent here (cache / virt_to_phys).
 *
 * sound_beep() fills ONE buffer and feeds it repeatedly (32000 B = 1 s at
 * 16 kHz mono 16-bit), so the stage copy is done ONCE per buffer (keyed on the
 * source pointer, reset per session by a1_sound_play). Copying on every feed
 * underran the TX FIFO and skipped once a second. stg[] holds a whole feed so
 * each feed is one paced PDMA transfer.
 */
static void *a1_stg_src;	/* last buffer staged; reset on a new session */

static int a1_aic_tx_data(struct udevice *i2s, void *data, uint size)
{
	struct a1_aic_priv *a = dev_get_priv(i2s);
	void __iomem *b = a->base;
	void __iomem *ch = (void __iomem *)PDMA_BASE;	/* channel 0 */
	static u8 stg[36 * 1024] __aligned(64);
	uint copy = size > sizeof(stg) ? sizeof(stg) : size;
	u32 spin;

	if (!(readl(b + AICCR) & AICCR_ERPL)) {
		rmw(b, AICSR, AICSR_TUR, 0);
		rmw(b, AICCR, 0, AICCR_TFLUSH);
	}

	if (data != a1_stg_src) {
		memcpy(stg, data, copy);
		flush_dcache_range((ulong)stg, (ulong)stg + sizeof(stg));
		a1_stg_src = data;
	}

	writel(0, ch + PDMA_CH_DCS);
	writel(virt_to_phys(stg), ch + PDMA_CH_DSA);
	writel(AICDR_PHYS, ch + PDMA_CH_DTA);
	writel(copy, ch + PDMA_CH_DTC);
	writel(PDMA_DRT_AIC_TX, ch + PDMA_CH_DRT);
	writel(PDMA_DCM_TX, ch + PDMA_CH_DCM);
	writel(PDMA_DCS_NDES | PDMA_DCS_CTE, ch + PDMA_CH_DCS);

	if (!(readl(b + AICCR) & AICCR_ERPL))
		rmw(b, AICCR, 0, AICCR_ERPL);

	for (spin = 80000000; spin; spin--)
		if (readl(ch + PDMA_CH_DCS) &
		    (PDMA_DCS_TT | PDMA_DCS_HLT | PDMA_DCS_AR))
			break;
	if (!spin)
		log_err("a1 TX DMA stalled (DCS=0x%08x)\n",
			readl(ch + PDMA_CH_DCS));
	writel(0, ch + PDMA_CH_DCS);
	return 0;
}

int a1_aic_stop(struct udevice *i2s)
{
	struct a1_aic_priv *a = dev_get_priv(i2s);

	rmw(a->base, AICCR, AICCR_ERPL, 0);
	rmw(a->base, AICSR, AICSR_TUR, 0);
	return 0;
}

static int a1_aic_probe(struct udevice *dev)
{
	struct a1_aic_priv *a = dev_get_priv(dev);
	struct i2s_uc_priv *uc = dev_get_uclass_priv(dev);
	fdt_addr_t addr = dev_read_addr(dev);

	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;
	a->base = map_physmem(addr, 0x100, MAP_NOCACHE);

	/* sound_beep buffer sizing: mono 16-bit 16 kHz. */
	uc->id = 0;
	uc->samplingrate = 16000;
	uc->bitspersample = 16;
	uc->channels = 1;
	uc->rfs = 256;
	uc->bfs = 32;
	uc->audio_pll_clk = 12000000;

	a1_aic_clk_init();
	a1_aic_init(a->base);
	return 0;
}

static const struct i2s_ops a1_aic_ops = {
	.tx_data = a1_aic_tx_data,
};

static const struct udevice_id a1_aic_ids[] = {
	{ .compatible = "ingenic,a1-aic" },
	{ }
};

U_BOOT_DRIVER(a1_aic) = {
	.name		= "a1_aic",
	.id		= UCLASS_I2S,
	.of_match	= a1_aic_ids,
	.probe		= a1_aic_probe,
	.ops		= &a1_aic_ops,
	.priv_auto	= sizeof(struct a1_aic_priv),
};

/* ============================ sound ============================ */

struct a1_sound_priv {
	bool playing;
};

static int a1_sound_setup(struct udevice *dev)
{
	struct sound_uc_priv *uc = dev_get_uclass_priv(dev);
	struct i2s_uc_priv *i2s = dev_get_uclass_priv(uc->i2s);
	int ret;

	if (uc->setup_done)
		return -EALREADY;

	/* Power up the codec (precharge), then bring up the AIC slave. */
	ret = audio_codec_set_params(uc->codec, i2s->id, i2s->samplingrate,
				     i2s->samplingrate * i2s->rfs,
				     i2s->bitspersample, i2s->channels);
	if (ret)
		return ret;
	a1_aic_enable(uc->i2s);

	uc->setup_done = true;
	return 0;
}

static int a1_sound_play(struct udevice *dev, void *data, uint data_size)
{
	struct sound_uc_priv *uc = dev_get_uclass_priv(dev);
	struct a1_sound_priv *priv = dev_get_priv(dev);

	/* Arm the DAC analog path (codec becomes I2S master) right before the
	 * first samples, then feed by DMA + replay. */
	if (!priv->playing) {
		a1_codec_enable_playback(uc->codec);
		a1_stg_src = NULL;	/* re-stage for this session */
		priv->playing = true;
	}
	return i2s_tx_data(uc->i2s, data, data_size);
}

static int a1_sound_stop_play(struct udevice *dev)
{
	struct sound_uc_priv *uc = dev_get_uclass_priv(dev);
	struct a1_sound_priv *priv = dev_get_priv(dev);

	a1_aic_stop(uc->i2s);
	if (priv->playing) {
		a1_codec_disable_playback(uc->codec);
		priv->playing = false;
	}
	return 0;
}

static int a1_sound_probe(struct udevice *dev)
{
	return sound_find_codec_i2s(dev);
}

static const struct sound_ops a1_sound_ops = {
	.setup		= a1_sound_setup,
	.play		= a1_sound_play,
	.stop_play	= a1_sound_stop_play,
};

static const struct udevice_id a1_sound_ids[] = {
	{ .compatible = "ingenic,a1-sound" },
	{ }
};

U_BOOT_DRIVER(a1_sound) = {
	.name		= "a1_sound",
	.id		= UCLASS_SOUND,
	.of_match	= a1_sound_ids,
	.probe		= a1_sound_probe,
	.ops		= &a1_sound_ops,
	.priv_auto	= sizeof(struct a1_sound_priv),
};
