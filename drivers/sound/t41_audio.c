// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T41 (XBurst2) audio - self-contained driver for the on-chip
 * "E-block" codec + AIC, a faithful port of the vendor SDK 4.4.94 oss3
 * (audio/t41/oss3: host/audio_aic.c + inner_codecs/codec.c).
 *
 * Kept SEPARATE from the shared XBurst1 jz_t10_codec.c / ingenic_i2s.c /
 * ingenic_sound.c: the XBurst2 E-block needs the vendor's exact integrated
 * bring-up ORDER, which the fragmented XBurst1 uclass path could not honour:
 *
 *   probe : program the I2S clocks + the AIC (configured, NOT enabled) +
 *           the PDMA engine.
 *   setup : codec power-up (codec_init), THEN enable the AIC unit.
 *   play  : codec_enable_playback, THEN feed the TX FIFO by PDMA + replay.
 *   stop  : codec_disable_playback.
 *
 * The vendor enables the AIC slave only after the codec (the I2S master) is
 * powered, and starts replay only after enable_playback - getting that order
 * wrong (as the XBurst1 path did, enabling the AIC at i2s-probe before the
 * codec exists) leaves the analog output collapsing after a moment even
 * though every register reads identical. Playback is fed by PDMA, the
 * vendor's method (the XBurst2 AIC is run by DMA, not PIO).
 *
 * Three DM drivers (codec / aic-i2s / sound) so U-Boot's sound uclass +
 * `sound` command work unchanged; the sound driver sequences the two.
 */

#define LOG_CATEGORY UCLASS_SOUND

#include <cpu_func.h>
#include <audio_codec.h>
#include <clk.h>
#include <dm.h>
#include <i2s.h>
#include <log.h>
#include <sound.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/err.h>

/* ---- register blocks (KSEG1 uncached) ---- */
#define CGU_BASE	0xb0000000
#define AIC_BASE	0xb0020000
#define PDMA_BASE	0xb3420000

/* CGU */
#define CGU_CLKGR0	0x20
#define CGU_I2STCDR	0x70
#define CGU_I2SRCDR	0x84
#define CLKGR0_AIC	BIT(11)
#define CLKGR0_PDMA	BIT(22)
/*
 * The I2S bit-clock comes from SCLKA/APLL through the CGU fractional
 * divider (out = parent * M / N). Keep the vendor numerator M = 32 and
 * compute N at probe from the live APLL (read via the clk uclass) so the
 * 256fs rate is correct on every SKU's APLL setpoint: T41A runs APLL
 * 1300 MHz, all other T41 SKUs 1104 MHz. The old hardcoded 0x220021b1
 * (M=32, N=8625) was only correct for the 1104 MHz parts.
 */
#define I2SCDR_M	32
#define I2SCDR_CE	BIT(29)
#define AIC_SYSCLK_HZ	(256 * 16000)	/* 256 fs @ 16 kHz = 4.096 MHz */

/* AIC */
#define AICFR		0x00
#define AICCR		0x04
#define I2SCR		0x10
#define AICSR		0x14
#define I2SDIV		0x30
#define AICDR		0x34
#define AICDR_PHYS	0x10020034
#define AICFR_ENB	BIT(0)
#define AICFR_RST	BIT(3)
#define AICFR_AUSEL	BIT(4)
#define AICFR_ICDC	BIT(5)
#define AICFR_LSMP	BIT(6)
#define AICFR_DMODE	BIT(8)		/* 0 = shared TX/RX clock */
#define AICFR_RMASTER	BIT(1)
#define AICFR_TMASTER	BIT(2)
#define AICFR_RFIFOS	BIT(13)		/* FIFO-reset status */
#define AICFR_TFIFOS	BIT(14)
#define AICFR_TFTH_SH	16
#define AICCR_EREC	BIT(0)
#define AICCR_ERPL	BIT(1)
#define AICCR_ENLBF	0		/* unused */
#define AICCR_TFLUSH	BIT(8)
#define AICCR_ASVTSU	BIT(9)
#define AICCR_ENDSW	BIT(10)
#define AICCR_M2S	BIT(11)
#define AICCR_TDMS	BIT(14)
#define AICCR_OSS_SH	19
#define AICCR_OSS_MSK	(0x7u << AICCR_OSS_SH)
#define AICCR_CHAN_SH	24
#define AICCR_CHAN_MSK	(0x7u << AICCR_CHAN_SH)
#define AICCR_CLEAR	(AICCR_TDMS | AICCR_ERPL | AICCR_EREC | \
			 BIT(3) | BIT(4) | BIT(5) | BIT(6))
#define I2SCR_AMSL	BIT(0)
#define I2SCR_RFIRST	BIT(17)
#define I2SCR_OR	BIT(31)		/* T41 live I2SCR = 0x80000000 */
#define AICSR_TUR	BIT(5)
#define AICSR_TFL_SH	8
#define AICSR_TFL_MSK	(0x3fu << 8)

/* PDMA (X2000/T41) */
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
/* DCM: SAI(src inc) | PORT_16 | TSZ_AUTO(7<<8) | RDIL=8(8<<16) (vendor) */
#define PDMA_DCM_TX	(BIT(23) | (2u << 14 | 2u << 12) | (7u << 8) | (8u << 16))

/* ---- E-block codec register offsets ---- */
#define E_CGR		0x00
#define E_CACR1		0x0c
#define E_CDCR		0x10
#define E_CDGSR		0x18
#define E_CDLCBMSR	0x1c
#define E_CDBCR		0x80
#define E_CCR		0x84
#define E_CAACR		0x88
#define E_CANACR1	0xa4
#define E_CHR		0xa8
#define E_CHPOUTLGR	0xac
#define E_CSRR		0x110

struct t41_codec_priv {
	void __iomem *base;
	u8 volume;
};

struct t41_aic_priv {
	void __iomem *base;	/* AIC */
};

/* bitfield read-modify-write with read-back (vendor codec_reg_set). */
static void cset(void __iomem *b, u32 reg, int s, int e, u32 val)
{
	u32 mask = (((1u << (e - s + 1)) - 1) << s);
	u32 v = readl(b + reg);

	v = (v & ~mask) | ((val << s) & mask);
	writel(v, b + reg);
}

static inline void rmw(void __iomem *b, u32 off, u32 clr, u32 set)
{
	writel((readl(b + off) & ~clr) | set, b + off);
}

/* ============================ codec ============================ */

static u32 t41_fs(int rate)
{
	static const int hz[8] = { 8000, 12000, 16000, 24000,
				   32000, 44100, 48000, 96000 };
	static const u32 fs[8] = { 7, 6, 5, 4, 3, 2, 1, 0 };
	int i;

	for (i = 0; i < 8; i++)
		if (hz[i] == rate)
			return fs[i];
	return 5;	/* 16 kHz */
}

/* vendor codec_init(): reset, dc-bias, charge-pump precharge ramp. ONCE. */
static void t41_codec_init(void __iomem *c)
{
	u32 value = 0;
	int i;

	cset(c, E_CGR, 0, 1, 0x0); mdelay(1);
	cset(c, E_CGR, 0, 1, 0x3); mdelay(1);
	cset(c, E_CANACR1, 4, 5, 0x1); mdelay(1);	/* DAC dc voltage */
	cset(c, E_CCR, 0, 7, 0x1); mdelay(1);
	cset(c, E_CDBCR, 6, 6, 0x1); mdelay(1);		/* reference voltage */
	for (i = 0; i <= 7; i++) {
		value |= value << 1 | 0x01;
		cset(c, E_CCR, 0, 7, value);
		mdelay(20);
	}
	cset(c, E_CAACR, 5, 5, 0x1); mdelay(10);	/* reference voltage */
	cset(c, E_CCR, 0, 7, 0x02); mdelay(20);		/* min charge current */
}

/* vendor codec_enable_playback(): DAC iface + analog driver chain. PER PLAY. */
static void t41_codec_enable_playback(struct udevice *dev)
{
	struct t41_codec_priv *p = dev_get_priv(dev);
	void __iomem *c = p->base;

	cset(c, E_CDCR, 3, 4, 0x2);	/* DAC iface = I2S */
	cset(c, E_CACR1, 6, 7, 0x3);	/* DAC I2S MASTER */
	cset(c, E_CANACR1, 7, 7, 0x1);	/* DAC current source */
	cset(c, E_CANACR1, 6, 6, 0x1);	/* DAC ref voltage buffer */
	cset(c, E_CANACR1, 4, 5, 0x2);	/* POP sound */
	cset(c, E_CHR, 4, 4, 0x1);	/* HPDRV enable */
	cset(c, E_CHR, 5, 5, 0x1);	/* HPDRV end-init */
	cset(c, E_CANACR1, 3, 3, 0x1);	/* DAC ref voltage */
	cset(c, E_CANACR1, 2, 2, 0x1);	/* DAC clock */
	cset(c, E_CANACR1, 1, 1, 0x1);	/* DAC module */
	mdelay(10);
	cset(c, E_CANACR1, 0, 0, 0x1);	/* end-init DAC */
	cset(c, E_CHR, 6, 6, 0x1);	/* unmute DRV */
	cset(c, E_CHPOUTLGR, 0, 4, p->volume);	/* HP gain */
	cset(c, E_CCR, 0, 7, 0x1);	/* low power consumption */
	cset(c, E_CDLCBMSR, 0, 0, 0x0);	/* spk noise mitigation */
}

static void t41_codec_disable_playback(struct udevice *dev)
{
	struct t41_codec_priv *p = dev_get_priv(dev);
	void __iomem *c = p->base;

	cset(c, E_CHPOUTLGR, 0, 4, 0x0);
	cset(c, E_CHR, 6, 6, 0x0);
	cset(c, E_CHR, 5, 5, 0x0);
	cset(c, E_CHR, 4, 4, 0x0);
	cset(c, E_CANACR1, 1, 1, 0x0);
	cset(c, E_CANACR1, 2, 2, 0x0);
	cset(c, E_CANACR1, 3, 3, 0x0);
	cset(c, E_CANACR1, 4, 5, 0x1);
	cset(c, E_CANACR1, 6, 6, 0x0);
	cset(c, E_CANACR1, 7, 7, 0x0);
	cset(c, E_CANACR1, 0, 0, 0x0);
}

static int t41_codec_set_params(struct udevice *dev, int interface, int rate,
				int mclk_freq, int bits_per_sample, uint channels)
{
	struct t41_codec_priv *p = dev_get_priv(dev);

	t41_codec_init(p->base);
	cset(p->base, E_CSRR, 0, 2, t41_fs(rate));
	return 0;
}

static int t41_codec_probe(struct udevice *dev)
{
	struct t41_codec_priv *p = dev_get_priv(dev);
	fdt_addr_t addr = dev_read_addr(dev);

	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;
	p->base = map_physmem(addr, 0x140, MAP_NOCACHE);
	p->volume = dev_read_u32_default(dev, "ingenic,replay-volume", 0x18) & 0x1f;
	return 0;
}

static const struct audio_codec_ops t41_codec_ops = {
	.set_params = t41_codec_set_params,
};

static const struct udevice_id t41_codec_ids[] = {
	{ .compatible = "ingenic,t41-codec" },
	{ }
};

U_BOOT_DRIVER(t41_codec) = {
	.name		= "t41_codec",
	.id		= UCLASS_AUDIO_CODEC,
	.of_match	= t41_codec_ids,
	.probe		= t41_codec_probe,
	.ops		= &t41_codec_ops,
	.priv_auto	= sizeof(struct t41_codec_priv),
};

/* ============================ AIC ============================ */

static void t41_aic_clk_init(u32 i2scdr)
{
	void __iomem *cgu = (void __iomem *)CGU_BASE;

	clrbits_le32(cgu + CGU_CLKGR0, CLKGR0_AIC | CLKGR0_PDMA);
	/* Latch the fractional divider cleanly (CE 0->1 edge). */
	writel(i2scdr & ~I2SCDR_CE, cgu + CGU_I2STCDR);
	writel(i2scdr, cgu + CGU_I2STCDR);
	writel(i2scdr & ~I2SCDR_CE, cgu + CGU_I2SRCDR);
	writel(i2scdr, cgu + CGU_I2SRCDR);
	/* Enable the PDMA engine (channels share it). */
	writel(PDMA_DMAC_DMAE, (void __iomem *)(PDMA_BASE + PDMA_DMAC));
}

/* Configure the AIC for the internal codec - NOT enabled (see t41_aic_enable).
 * Mirrors the vendor audio_aic.c init order. */
static void t41_aic_init(void __iomem *b)
{
	u32 tmo = 100000;

	rmw(b, AICFR, AICFR_ENB, 0);
	rmw(b, AICFR, 0, AICFR_RST);
	while ((readl(b + AICFR) & (AICFR_RFIFOS | AICFR_TFIFOS)) && --tmo)
		;
	rmw(b, AICFR, AICFR_RST, 0);

	rmw(b, AICFR, 0, AICFR_AUSEL);			/* I2S unit */
	rmw(b, I2SCR, I2SCR_AMSL, 0);			/* I2S format */
	rmw(b, AICFR, 0, AICFR_ICDC);			/* internal codec */
	rmw(b, AICFR, AICFR_RMASTER | AICFR_TMASTER, 0);/* AIC = I2S slave */
	rmw(b, AICFR, AICFR_DMODE, 0);			/* shared TX/RX clock */

	/* 16-bit, signed, little-endian, mono + mono->stereo expand. */
	rmw(b, AICCR, AICCR_OSS_MSK, 1u << AICCR_OSS_SH);
	rmw(b, AICCR, AICCR_ENDSW | AICCR_ASVTSU, 0);
	rmw(b, AICCR, AICCR_CHAN_MSK, 0);		/* mono */
	rmw(b, AICCR, 0, AICCR_M2S);			/* mono -> stereo */

	rmw(b, AICCR, AICCR_CLEAR, 0);			/* no dma/replay/irq yet */
	rmw(b, AICSR, AICSR_TUR, 0);
	rmw(b, AICFR, 0x1fu << AICFR_TFTH_SH, 16u << AICFR_TFTH_SH);

	rmw(b, I2SCR, I2SCR_RFIRST, 0);			/* left-first */
	rmw(b, I2SCR, 0, I2SCR_OR);			/* T41 I2SCR bit31 */
	writel(0x40004, b + I2SDIV);
	rmw(b, AICFR, 0, AICFR_LSMP);			/* repeat last on underrun */
	rmw(b, AICCR, 0, AICCR_TDMS);			/* TX fed by DMA */
}

/* Enable the AIC unit - called from sound setup() AFTER codec power-up, so
 * the AIC slave comes up with the codec (I2S master) already running. */
int t41_aic_enable(struct udevice *i2s)
{
	struct t41_aic_priv *a = dev_get_priv(i2s);

	rmw(a->base, AICFR, 0, AICFR_ENB);
	return 0;
}

/* Feed one buffer to the TX FIFO via a one-shot PDMA transfer, paced by the
 * AIC_TX request, then enable replay on the first buffer. */
static int t41_aic_tx_data(struct udevice *i2s, void *data, uint size)
{
	struct t41_aic_priv *a = dev_get_priv(i2s);
	void __iomem *b = a->base;
	void __iomem *ch = (void __iomem *)PDMA_BASE;	/* channel 0 */
	u32 spin;

	flush_dcache_range((ulong)data, (ulong)data + size);

	if (!(readl(b + AICCR) & AICCR_ERPL)) {
		rmw(b, AICSR, AICSR_TUR, 0);
		rmw(b, AICCR, 0, AICCR_TFLUSH);
	}

	writel(0, ch + PDMA_CH_DCS);
	writel(virt_to_phys(data), ch + PDMA_CH_DSA);
	writel(AICDR_PHYS, ch + PDMA_CH_DTA);
	writel(size, ch + PDMA_CH_DTC);		/* TSZ_AUTO: count in bytes */
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
		log_err("t41 TX DMA stalled (DCS=0x%08x)\n",
			readl(ch + PDMA_CH_DCS));
	writel(0, ch + PDMA_CH_DCS);
	return 0;
}

int t41_aic_stop(struct udevice *i2s)
{
	struct t41_aic_priv *a = dev_get_priv(i2s);

	rmw(a->base, AICCR, AICCR_ERPL, 0);
	rmw(a->base, AICSR, AICSR_TUR, 0);
	return 0;
}

static int t41_aic_probe(struct udevice *dev)
{
	struct t41_aic_priv *a = dev_get_priv(dev);
	struct i2s_uc_priv *uc = dev_get_uclass_priv(dev);
	fdt_addr_t addr = dev_read_addr(dev);
	struct clk apll;
	unsigned long rate;
	u32 i2scdr;
	int ret;

	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;
	a->base = map_physmem(addr, 0x100, MAP_NOCACHE);

	/*
	 * Derive the I2SCDR from the live APLL instead of a hardcoded word,
	 * so the 256fs bit-clock is right on every SKU's APLL setpoint
	 * (out = APLL * M / N, M fixed at 32, N solved for 256*16 kHz).
	 */
	ret = clk_get_by_name(dev, "apll", &apll);
	if (ret)
		return ret;
	rate = clk_get_rate(&apll);
	if (IS_ERR_VALUE(rate) || !rate)
		return -EINVAL;
	i2scdr = I2SCDR_CE | (I2SCDR_M << 20) |
		 ((u32)(((u64)I2SCDR_M * rate) / AIC_SYSCLK_HZ) & 0xfffff);

	/* Params for sound_beep's buffer sizing: mono 16-bit 16 kHz. */
	uc->id = 0;
	uc->samplingrate = 16000;
	uc->bitspersample = 16;
	uc->channels = 1;
	uc->rfs = 256;
	uc->bfs = 32;
	uc->audio_pll_clk = 12000000;

	t41_aic_clk_init(i2scdr);
	t41_aic_init(a->base);
	return 0;
}

static const struct i2s_ops t41_aic_ops = {
	.tx_data = t41_aic_tx_data,
};

static const struct udevice_id t41_aic_ids[] = {
	{ .compatible = "ingenic,t41-aic" },
	{ }
};

U_BOOT_DRIVER(t41_aic) = {
	.name		= "t41_aic",
	.id		= UCLASS_I2S,
	.of_match	= t41_aic_ids,
	.probe		= t41_aic_probe,
	.ops		= &t41_aic_ops,
	.priv_auto	= sizeof(struct t41_aic_priv),
};

/* ============================ sound ============================ */

struct t41_sound_priv {
	bool playing;
};

static int t41_sound_setup(struct udevice *dev)
{
	struct sound_uc_priv *uc = dev_get_uclass_priv(dev);
	struct i2s_uc_priv *i2s = dev_get_uclass_priv(uc->i2s);
	int ret;

	if (uc->setup_done)
		return -EALREADY;

	/* Vendor order: power up the codec (it masters BITCLK), THEN enable
	 * the AIC slave. */
	ret = audio_codec_set_params(uc->codec, i2s->id, i2s->samplingrate,
				     i2s->samplingrate * i2s->rfs,
				     i2s->bitspersample, i2s->channels);
	if (ret)
		return ret;
	t41_aic_enable(uc->i2s);

	uc->setup_done = true;
	return 0;
}

static int t41_sound_play(struct udevice *dev, void *data, uint data_size)
{
	struct sound_uc_priv *uc = dev_get_uclass_priv(dev);
	struct t41_sound_priv *priv = dev_get_priv(dev);

	/* Arm the DAC analog path right before the first samples (vendor
	 * set_codec_spk_start order: enable_playback, then replay+DMA). */
	if (!priv->playing) {
		t41_codec_enable_playback(uc->codec);
		priv->playing = true;
	}
	return i2s_tx_data(uc->i2s, data, data_size);
}

static int t41_sound_stop_play(struct udevice *dev)
{
	struct sound_uc_priv *uc = dev_get_uclass_priv(dev);
	struct t41_sound_priv *priv = dev_get_priv(dev);

	t41_aic_stop(uc->i2s);
	if (priv->playing) {
		t41_codec_disable_playback(uc->codec);
		priv->playing = false;
	}
	return 0;
}

static int t41_sound_probe(struct udevice *dev)
{
	return sound_find_codec_i2s(dev);
}

static const struct sound_ops t41_sound_ops = {
	.setup		= t41_sound_setup,
	.play		= t41_sound_play,
	.stop_play	= t41_sound_stop_play,
};

static const struct udevice_id t41_sound_ids[] = {
	{ .compatible = "ingenic,t41-sound" },
	{ }
};

U_BOOT_DRIVER(t41_sound) = {
	.name		= "t41_sound",
	.id		= UCLASS_SOUND,
	.of_match	= t41_sound_ids,
	.probe		= t41_sound_probe,
	.ops		= &t41_sound_ops,
	.priv_auto	= sizeof(struct t41_sound_priv),
};
