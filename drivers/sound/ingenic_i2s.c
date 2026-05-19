// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst1 AIC (I2S) transmit driver - PIO playback.
 *
 * Faithful port of the playback path of the vendor SDK 3.10.14 OSS2
 * driver (audio/t31/oss2/devices/xb47xx_i2s_v12.c). The on-chip codec
 * is the I2S master on T10/T20/T21/T30 (the codec self-derives the
 * audio clock from its 12 MHz domain), so the AIC runs as I2S clock
 * slave with SYSCLK output enabled and I2SDIV is unused. Samples are
 * fed by PIO into the TX FIFO (AICDR); no DMA in U-Boot.
 *
 * The AIC functional clock is brought up directly (ungate CLKGR0 +
 * I2SCDR change-enable), the same faithful-direct pattern the MSC0 /
 * MAC-PHY clocks use in this port - not via a clk-uclass leaf, since
 * I2SCDR is a fractional M/N divider unlike the integer-divider CGU
 * leaves modelled by clk-tXX.
 */

#define LOG_CATEGORY UCLASS_I2S

#include <dm.h>
#include <i2s.h>
#include <log.h>
#include <sound.h>
#include <asm/io.h>
#include <linux/delay.h>

/* AIC register offsets (xb47xx_i2s_v12.h). */
#define AICFR		0x00
#define AICCR		0x04
#define I2SCR		0x10
#define AICSR		0x14
#define I2SDIV		0x30
#define AICDR		0x34

/* AICFR */
#define AICFR_ENB	BIT(0)
#define AICFR_RST	BIT(3)
#define AICFR_AUSEL	BIT(4)	/* 1 = I2S (not AC-link) */
#define AICFR_ICDC	BIT(5)	/* internal-codec data path */
#define AICFR_LSMP	BIT(6)	/* underrun: repeat last sample */
#define AICFR_ICS	BIT(7)	/* internal codec select */
#define AICFR_SYNCD	BIT(1)	/* SYNC dir  0=in(slave) */
#define AICFR_BCKD	BIT(2)	/* BITCLK dir 0=in(slave) */
#define AICFR_ISYNCD	BIT(9)
#define AICFR_IBCKD	BIT(10)
#define AICFR_TFTH_SH	16
#define AICFR_TFTH_MASK	(0x1fu << AICFR_TFTH_SH)

/* AICCR */
#define AICCR_ERPL	BIT(1)	/* enable replay (TX) */
#define AICCR_ETUR	BIT(5)
#define AICCR_EROR	BIT(6)
#define AICCR_ETFS	BIT(3)
#define AICCR_ERFS	BIT(4)
#define AICCR_TFLUSH	BIT(8)
#define AICCR_ASVTSU	BIT(9)	/* sign adjust (0 = signed S16) */
#define AICCR_ENDSW	BIT(10)	/* byte swap (0 = little-endian) */
#define AICCR_M2S	BIT(11)	/* mono -> stereo expand */
#define AICCR_TDMS	BIT(14)	/* TX DMA (0 = PIO) */
#define AICCR_OSS_SH	19
#define AICCR_OSS_MASK	(0x7u << AICCR_OSS_SH)	/* 1 = 16-bit */
#define AICCR_CHAN_SH	24
#define AICCR_CHAN_MASK	(0x7u << AICCR_CHAN_SH)	/* channels - 1 */

/* I2SCR */
#define I2SCR_AMSL	BIT(0)	/* 0 = I2S format */
#define I2SCR_ESCLK	BIT(4)	/* output SYSCLK to codec */
#define I2SCR_STPBK	BIT(12)	/* stop BITCLK */
#define I2SCR_ISTPBK	BIT(13)
#define I2SCR_RFIRST	BIT(17)

/* AICSR */
#define AICSR_TUR	BIT(5)	/* TX underrun (write 0 to clear) */
#define AICSR_TFL_SH	8
#define AICSR_TFL_MASK	(0x3fu << AICSR_TFL_SH)	/* TX FIFO level, depth 64 */

#define AIC_TX_FIFO_DEPTH	64
#define AIC_TX_TRIGGER		30

/*
 * CPM: AIC peripheral gate + I2S CGU divider.
 *
 * T10/T20/T21/T30 keep a single I2SCDR at 0x60 (TX+RX share one
 * divider). T23/T31/C100 deleted I2SCDR@0x60 and split it into two
 * independent dividers: I2SSPKCDR@0x70 (TX/speaker) and I2SMICCDR@
 * 0x84 (RX/mic). Both halves share the same M[28:20]/N[19:0]/CE[29]
 * field layout as the unified register (kernel
 * arch/mips/xburst/soc-t31/common/clk/clk_cgu.c, vendor U-Boot
 * arch/mips/include/asm/arch-t31/cpm.h). Vendor kernel always
 * clk_enable()s BOTH halves on T31/C100 even for playback-only;
 * mirror that.
 */
#define CPM_BASE		0xb0000000
#define CPM_CLKGR0		0x20
#define CPM_CLKGR0_AIC		BIT(11)
#define I2SCDR_SRC_MASK		(3u << 30)
#define I2SCDR_SRC_MPLL		(1u << 30)
#define I2SCDR_CE		BIT(29)
#define I2SCDR_M_SH		20		/* numerator   [28:20] */
#define I2SCDR_M_MASK		(0x1ffu << I2SCDR_M_SH)
#define I2SCDR_N_MASK		0xfffffu	/* denominator [19:0] */

/*
 * Internal-codec SYSCLK = fs * 256, exactly what the vendor feeds
 * (clk_set_rate(cgu_i2s, rate*256) per stream). The codec, as I2S
 * master, divides SYSCLK to BITCLK = 64*fs / SYNC = fs, so it expects
 * 256*fs - a fixed ~12 MHz is NOT equivalent. Driver runs 44100 Hz.
 */
#define AIC_SYSCLK_HZ		(44100u * 256u)		/* 11.2896 MHz */

struct ingenic_i2s_soc {
	u32 mpll_hz;		/* I2SCDR parent (MPLL) rate for this SoC */
	u32 spk_cdr_off;	/* TX clk divider: 0x60 unified or 0x70 spk */
	u32 mic_cdr_off;	/* RX clk divider: 0 (unified) or 0x84 mic */
};

struct ingenic_i2s_priv {
	void __iomem *base;
	const struct ingenic_i2s_soc *soc;
	bool		playing;	/* ERPL enabled, drop on stop_play */
};

static inline void aic_rmw(void __iomem *base, u32 off, u32 clr, u32 set)
{
	u32 v = readl(base + off);

	v = (v & ~clr) | set;
	writel(v, base + off);
}

static void ingenic_aic_cdr_program(void __iomem *cpm, u32 off, u32 n)
{
	u32 v = readl(cpm + off);

	v &= ~(I2SCDR_SRC_MASK | I2SCDR_M_MASK | I2SCDR_N_MASK);
	v |= I2SCDR_SRC_MPLL | (1u << I2SCDR_M_SH) |
	     (n & I2SCDR_N_MASK) | I2SCDR_CE;
	writel(v, cpm + off);
	/* The M/N decimal divider has no BUSY/poll; CE makes it live. */
}

static void ingenic_aic_clk_init(struct ingenic_i2s_priv *priv)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 n;

	/* Ungate the AIC peripheral clock (gated -> first AIC access
	 * bus-stalls, same failure class as the MSC0 clock). */
	clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_AIC);

	/*
	 * Program the I2S CGU divider(s): source = MPLL, M = 1, N chosen
	 * so the output is ~SYSCLK (output = parent * M / N). The SPL
	 * / bootrom does not set this up for the audio path, so a bare
	 * change-enable on an unprogrammed divider yields NO SYSCLK -
	 * the internal codec (I2S master) then never produces BITCLK
	 * and the TX FIFO never drains. Leave CE set afterwards (same
	 * rule as the MSC0/MAC-PHY dividers).
	 */
	n = priv->soc->mpll_hz / AIC_SYSCLK_HZ;
	if (!n)
		n = 1;
	if (n > I2SCDR_N_MASK)
		n = I2SCDR_N_MASK;

	ingenic_aic_cdr_program(cpm, priv->soc->spk_cdr_off, n);
	if (priv->soc->mic_cdr_off)
		ingenic_aic_cdr_program(cpm, priv->soc->mic_cdr_off, n);
}

static int ingenic_i2s_init(struct ingenic_i2s_priv *priv)
{
	void __iomem *b = priv->base;

	ingenic_aic_clk_init(priv);

	/* Disable, soft-reset (pulse, then clear so the RMW writes
	 * below don't keep the AIC held in reset), hold bit clocks. */
	aic_rmw(b, AICFR, AICFR_ENB, 0);
	aic_rmw(b, AICFR, 0, AICFR_RST);
	mdelay(1);
	aic_rmw(b, AICFR, AICFR_RST, 0);
	aic_rmw(b, I2SCR, 0, I2SCR_STPBK | I2SCR_ISTPBK);

	/* I2S unit, I2S format, internal codec as MASTER (ICS|ICDC),
	 * AIC as the I2S clock slave, feed SYSCLK out to the codec. */
	aic_rmw(b, AICFR, 0, AICFR_AUSEL);
	aic_rmw(b, I2SCR, I2SCR_AMSL, 0);
	aic_rmw(b, AICFR, 0, AICFR_ICS | AICFR_ICDC);
	aic_rmw(b, AICFR, AICFR_BCKD | AICFR_SYNCD | AICFR_ISYNCD |
		 AICFR_IBCKD, 0);
	aic_rmw(b, I2SCR, 0, I2SCR_ESCLK);

	/* 16-bit, signed, little-endian, stereo (no mono->stereo). */
	aic_rmw(b, AICCR, AICCR_OSS_MASK, 1u << AICCR_OSS_SH);
	aic_rmw(b, AICCR, AICCR_ENDSW | AICCR_ASVTSU, 0);
	aic_rmw(b, AICCR, AICCR_CHAN_MASK | AICCR_M2S,
		(2 - 1) << AICCR_CHAN_SH);

	/* Quiesce: no DMA, no replay/record yet, no IRQs (PIO). */
	aic_rmw(b, AICCR, AICCR_TDMS | AICCR_ERPL, 0);
	aic_rmw(b, AICSR, AICSR_TUR, 0);
	aic_rmw(b, AICFR, AICFR_TFTH_MASK,
		AIC_TX_TRIGGER << AICFR_TFTH_SH);
	aic_rmw(b, AICCR, AICCR_ETUR | AICCR_EROR | AICCR_ETFS |
		AICCR_ERFS, 0);

	/* Vendor framing; repeat last sample on underrun; run clocks. */
	aic_rmw(b, I2SCR, 0, I2SCR_RFIRST);
	aic_rmw(b, AICFR, 0, AICFR_LSMP);
	aic_rmw(b, I2SCR, I2SCR_STPBK | I2SCR_ISTPBK, 0);
	aic_rmw(b, AICFR, 0, AICFR_ENB);

	return 0;
}

static int ingenic_i2s_tx_data(struct udevice *dev, void *data,
			       uint data_size)
{
	struct ingenic_i2s_priv *priv = dev_get_priv(dev);
	void __iomem *b = priv->base;
	const u16 *s = data;
	uint n = data_size / sizeof(u16);	/* one 16-bit word/sample */

	/*
	 * U-Boot's sound_beep() splits long tones into 1-second buffers
	 * and calls sound_play() repeatedly. Toggling AICCR_ERPL between
	 * each buffer is audible on the B-block codec (T31/C100/T23) as
	 * silence gaps - the codec mutes briefly on every ERPL drop.
	 * Solution: flush + enable replay ONCE on the first feed and
	 * leave ERPL set; only ingenic_i2s_stop_play() actually stops
	 * it. The TX FIFO already paces the writes below, so no drain
	 * is needed between buffers - the next call just refills.
	 */
	if (!priv->playing) {
		aic_rmw(b, AICCR, 0, AICCR_TFLUSH);
		aic_rmw(b, AICCR, 0, AICCR_ERPL);
		priv->playing = true;
	}

	while (n--) {
		u32 spin = 2000000;

		/*
		 * TFL is a 6-bit level (0..63); a 64-deep FIFO reads 63
		 * when full, so "no room" is TFL >= DEPTH-1, not >= DEPTH
		 * (the latter is never true -> the loop never paces and
		 * samples are blasted at a full FIFO and lost).
		 */
		while (((readl(b + AICSR) & AICSR_TFL_MASK) >>
			AICSR_TFL_SH) >= AIC_TX_FIFO_DEPTH - 1) {
			if (!--spin) {
				aic_rmw(b, AICCR, AICCR_ERPL, 0);
				priv->playing = false;
				log_err("AIC TX FIFO stalled (no codec BITCLK?)\n");
				return -ETIMEDOUT;
			}
		}
		writel(*s++, b + AICDR);
	}

	return 0;
}

int ingenic_i2s_stop_play(struct udevice *dev)
{
	struct ingenic_i2s_priv *priv = dev_get_priv(dev);
	void __iomem *b = priv->base;
	u32 tmo = 2000000;

	if (!priv->playing)
		return 0;

	/* Drain any pending samples, then disable replay. */
	while ((readl(b + AICSR) & AICSR_TFL_MASK) >> AICSR_TFL_SH) {
		if (!--tmo)
			break;
	}
	aic_rmw(b, AICCR, AICCR_ERPL, 0);
	priv->playing = false;
	return 0;
}

static int ingenic_i2s_probe(struct udevice *dev)
{
	struct ingenic_i2s_priv *priv = dev_get_priv(dev);
	struct i2s_uc_priv *uc = dev_get_uclass_priv(dev);
	fdt_addr_t addr;

	priv->soc = (const struct ingenic_i2s_soc *)dev_get_driver_data(dev);

	addr = dev_read_addr(dev);
	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;
	priv->base = map_physmem(addr, 0x100, MAP_NOCACHE);

	uc->id = 0;
	uc->samplingrate = 44100;
	uc->bitspersample = 16;
	uc->channels = 2;
	uc->rfs = 256;
	uc->bfs = 32;
	uc->audio_pll_clk = 12000000;

	return ingenic_i2s_init(priv);
}

static const struct i2s_ops ingenic_i2s_ops = {
	.tx_data	= ingenic_i2s_tx_data,
};

/*
 * I2SCDR parent (MPLL) rate per SoC - only needs to be close enough
 * to land the SYSCLK near 12 MHz; the codec re-derives the audio fs.
 * Values per the mainline T-series PLL setup (clk-tXX / tXX pll.c).
 * spk_cdr_off: 0x60 (T10/T20/T21/T30 unified) or 0x70 (T23/T31/C100
 * split TX). mic_cdr_off: 0 (unified) or 0x84 (split RX).
 */
#define I2S_CDR_UNIFIED		0x60
#define I2S_SPK_CDR_SPLIT	0x70
#define I2S_MIC_CDR_SPLIT	0x84
static const struct ingenic_i2s_soc soc_t10  = { .mpll_hz = 1200000000u,
	.spk_cdr_off = I2S_CDR_UNIFIED };
static const struct ingenic_i2s_soc soc_t20  = { .mpll_hz = 1000000000u,
	.spk_cdr_off = I2S_CDR_UNIFIED };
static const struct ingenic_i2s_soc soc_t21  = { .mpll_hz =  900000000u,
	.spk_cdr_off = I2S_CDR_UNIFIED };
static const struct ingenic_i2s_soc soc_t23  = { .mpll_hz = 1200000000u,
	.spk_cdr_off = I2S_SPK_CDR_SPLIT,
	.mic_cdr_off = I2S_MIC_CDR_SPLIT };
static const struct ingenic_i2s_soc soc_t30  = { .mpll_hz = 1000000000u,
	.spk_cdr_off = I2S_CDR_UNIFIED };
static const struct ingenic_i2s_soc soc_t31  = { .mpll_hz = 1200000000u,
	.spk_cdr_off = I2S_SPK_CDR_SPLIT,
	.mic_cdr_off = I2S_MIC_CDR_SPLIT };
static const struct ingenic_i2s_soc soc_c100 = { .mpll_hz = 1200000000u,
	.spk_cdr_off = I2S_SPK_CDR_SPLIT,
	.mic_cdr_off = I2S_MIC_CDR_SPLIT };

static const struct udevice_id ingenic_i2s_ids[] = {
	{ .compatible = "ingenic,t10-aic",  .data = (ulong)&soc_t10 },
	{ .compatible = "ingenic,t20-aic",  .data = (ulong)&soc_t20 },
	{ .compatible = "ingenic,t21-aic",  .data = (ulong)&soc_t21 },
	{ .compatible = "ingenic,t23-aic",  .data = (ulong)&soc_t23 },
	{ .compatible = "ingenic,t30-aic",  .data = (ulong)&soc_t30 },
	{ .compatible = "ingenic,t31-aic",  .data = (ulong)&soc_t31 },
	{ .compatible = "ingenic,c100-aic", .data = (ulong)&soc_c100 },
	{ }
};

U_BOOT_DRIVER(ingenic_i2s) = {
	.name		= "ingenic_i2s",
	.id		= UCLASS_I2S,
	.of_match	= ingenic_i2s_ids,
	.probe		= ingenic_i2s_probe,
	.ops		= &ingenic_i2s_ops,
	.priv_auto	= sizeof(struct ingenic_i2s_priv),
};
