// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst1 on-chip audio codec ("jz_t10" family) - playback.
 *
 * Faithful port of the playback bring-up of the vendor SDK 3.10.14
 * OSS2 codec (audio/t31/oss2/devices/codecs/jz_t10_codec.c). The codec
 * is reached by direct MMIO at its own register block (NOT the AIC
 * RGADW/RGDATA indirect path, which the vendor uses only for other
 * codec variants). set_params() runs the vendor power-up: clock
 * enable, DAC I2S-master config, the SoC-specific charge-pump ramp,
 * the DAC->speaker route + replay volume, then asserts the external
 * speaker-amp enable GPIO last (anti-pop).
 *
 * The msleep() values in the vendor sequence are real-silicon analog
 * settling / charge-pump ramp requirements and are kept as mdelay().
 *
 * Register/ramp/route differ in two families:
 *   A-block: T10/T20/T21/T30  (CCR=CCR_28 0xa0, vol=CHCR_27 0x9c[4:0])
 *   B-block: T31/C100         (CCR=CCR_21 0x84, vol=CHR_28  0xa0[4:0])
 */

#define LOG_CATEGORY UCLASS_AUDIO_CODEC

#include <audio_codec.h>
#include <dm.h>
#include <log.h>
#include <asm/io.h>
#include <linux/delay.h>

/* A-block codec register offsets (T10/T20/T21/T30). */
#define A_CGR_00	0x00
#define A_CMCR_03	0x0c
#define A_CDCR1_04	0x10
#define A_CDCR2_05	0x14
#define A_CAACR_21	0x84
#define A_CAR_25	0x94
#define A_CHR_26	0x98
#define A_CHCR_27	0x9c
#define A_CCR_28	0xa0
#define A_CSRR_44	0x110

/* B-block codec register offsets (T31/C100). */
#define B_CGR_00	0x00
#define B_CACR2_03	0x0c
#define B_CDCR1_04	0x10
#define B_CDCR2_05	0x14
#define B_POWER_20	0x80
#define B_CCR_21	0x84
#define B_CAACR_22	0x88
#define B_CMICCR_23	0x8c
#define B_CANACR_26	0x98
#define B_CANACR2_27	0x9c
#define B_CHR_28	0xa0
#define B_CSRR_44	0x110

/* CPM AIC gate - the codec island shares it; ungate defensively. */
#define CPM_BASE	0xb0000000
#define CPM_CLKGR0	0x20
#define CPM_CLKGR0_AIC	BIT(11)

/*
 * Speaker-amp GPIO. Parsed from DT (`ingenic,spk-gpio = <&gpb N FLAGS>`)
 * and driven RAW because the XBurst1 GPIO port stride is SoC-specific
 * and the kernel/U-Boot pinctrl-ingenic uclass would mux differently:
 *   T10/T20 -> 0x100 (older silicon, validated on T20X HW by md on
 *              PXPIN bit toggling 0 -> 1)
 *   T21/T23/T30/T31/C100 -> 0x1000 (newer silicon, kernel
 *              GPIO_PORT_OFF; using 0x100 here hits aliased/garbage
 *              MMIO, the GPIO never flips, the amp stays muted and
 *              T31 produces no audible output even though BITCLK and
 *              FIFO drain correctly - validated on Wyze V3 by direct
 *              PXMSK readback after the writes).
 */
#define GPIO_BASE_PHYS		0xb0010000u
#define G_PXINTC		0x18	/* INT clear  -> not interrupt */
#define G_PXMSKS		0x24	/* MSK set    -> GPIO (not device) */
#define G_PXPAT1C		0x38	/* PAT1 clear -> output */
#define G_PXPAT0S		0x44	/* PAT0 set   -> drive high */
#define G_PXPAT0C		0x48	/* PAT0 clear -> drive low  */

enum jz_codec_soc {
	JZ_CODEC_T20,	/* A-block, rising 0x3f>>(6-i) ramp */
	JZ_CODEC_T10,	/* A-block, falling 0x3f>>i ramp, CCR bit7=0 */
	JZ_CODEC_T30_T21,/* A-block, value|=value<<1|1 ramp */
	JZ_CODEC_T31_C100,/* B-block */
};

struct jz_codec_priv {
	void __iomem *base;
	enum jz_codec_soc soc;
	bool		has_spk;
	bool		spk_active_low;
	u8		spk_port;	/* 0=A 1=B 2=C 3=D */
	u8		spk_pin;	/* 0..31 */
	u8		volume;		/* 0..0x1f (CHCR_27[4:0] / CHR_28[4:0]) */
};

/* Raw GPIO drive at the per-SoC port stride. */
static u32 jz_codec_port_stride(enum jz_codec_soc soc)
{
	/* T10/T20 = 0x100; T21/T23/T30/T31/C100 = 0x1000 (kernel
	 * GPIO_PORT_OFF per arch/mips/xburst/soc-tXX/common/gpio.c). */
	return (soc == JZ_CODEC_T10 || soc == JZ_CODEC_T20) ? 0x100u
							    : 0x1000u;
}

static void jz_codec_spk_drive(struct jz_codec_priv *p, bool on)
{
	void __iomem *gpx;
	u32 bit;
	bool high;

	if (!p->has_spk)
		return;

	gpx  = (void __iomem *)(GPIO_BASE_PHYS +
				p->spk_port * jz_codec_port_stride(p->soc));
	bit  = BIT(p->spk_pin);
	high = on ^ p->spk_active_low;	/* XOR: invert if active-low */

	writel(bit, gpx + G_PXINTC);		/* not interrupt */
	writel(bit, gpx + G_PXMSKS);		/* GPIO mode */
	writel(bit, gpx + G_PXPAT1C);		/* output */
	writel(bit, gpx + (high ? G_PXPAT0S : G_PXPAT0C));
}

/* Vendor codec_reg_set: bitfield read-modify-write with read-back. */
static void codec_set(struct jz_codec_priv *p, u32 reg, int start,
		      int end, u32 val)
{
	u32 mask = ((1u << (end - start + 1)) - 1) << start;
	u32 v = readl(p->base + reg);

	v = (v & ~mask) | ((val << start) & mask);
	writel(v, p->base + reg);
	if ((readl(p->base + reg) & mask) != ((val << start) & mask))
		log_debug("codec reg 0x%x readback mismatch\n", reg);
}

/* fs select for CSRR: rate -> field value. */
static u32 jz_codec_fs(int rate)
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

/* A-block (T10/T20/T21/T30) power-up + DAC->speaker route + volume. */
static void jz_codec_a_powerup(struct jz_codec_priv *p)
{
	int i;

	codec_set(p, A_CGR_00, 0, 1, 0x0);
	mdelay(10);
	codec_set(p, A_CGR_00, 0, 1, 0x3);
	mdelay(10);

	codec_set(p, A_CMCR_03, 0, 7, 0x3e);	/* DAC I2S MASTER */
	codec_set(p, A_CDCR1_04, 0, 7, 0x10);	/* DAC iface = I2S */
	codec_set(p, A_CDCR2_05, 0, 7, 0x0e);
	codec_set(p, A_CHCR_27, 6, 7, 0x1);	/* precharge */
	mdelay(10);

	/* Charge-pump ramp - SoC specific (vendor init_codec). */
	if (p->soc == JZ_CODEC_T20) {
		codec_set(p, A_CCR_28, 7, 7, 0x1);
		mdelay(10);
		for (i = 0; i < 6; i++) {
			codec_set(p, A_CCR_28, 0, 6, 0x3f >> (6 - i));
			mdelay(30);
		}
		codec_set(p, A_CCR_28, 0, 6, 0x3f);
	} else if (p->soc == JZ_CODEC_T10) {
		codec_set(p, A_CCR_28, 7, 7, 0x0);
		mdelay(10);
		codec_set(p, A_CCR_28, 0, 6, 0x3f);
		mdelay(10);
		for (i = 0; i < 6; i++) {
			codec_set(p, A_CCR_28, 0, 6, 0x3f >> i);
			mdelay(30);
		}
		codec_set(p, A_CCR_28, 0, 6, 0x0);
	} else {	/* JZ_CODEC_T30_T21 */
		u32 value = 0;

		codec_set(p, A_CCR_28, 0, 6, 1);
		mdelay(10);
		codec_set(p, A_CCR_28, 7, 7, 1);
		mdelay(10);
		for (i = 0; i < 6; i++) {
			value |= value << 1 | 1;
			codec_set(p, A_CCR_28, 0, 6, value);
			mdelay(30);
		}
		codec_set(p, A_CCR_28, 0, 6, 1);
	}
	mdelay(20);

	/* DAC -> speaker route + replay volume (vendor codec_set_speaker). */
	codec_set(p, A_CAACR_21, 6, 7, 0x3);
	mdelay(10);
	codec_set(p, A_CAR_25, 6, 6, 1);	/* current source */
	mdelay(10);
	codec_set(p, A_CAR_25, 5, 5, 1);	/* reference voltage */
	mdelay(10);
	codec_set(p, A_CHCR_27, 6, 7, 2);	/* anti-pop control */
	mdelay(10);
	codec_set(p, A_CAR_25, 3, 3, 1);
	mdelay(10);
	codec_set(p, A_CAR_25, 2, 2, 1);
	mdelay(10);
	codec_set(p, A_CAR_25, 1, 1, 1);
	mdelay(10);
	codec_set(p, A_CAR_25, 0, 0, 1);	/* DAC stages on */
	mdelay(10);
	codec_set(p, A_CHR_26, 7, 7, 1);
	mdelay(10);
	codec_set(p, A_CHR_26, 6, 6, 1);
	mdelay(10);
	codec_set(p, A_CHR_26, 5, 5, 1);	/* HP/LO output on */
	mdelay(10);
	codec_set(p, A_CHCR_27, 0, 4, p->volume);
}

/* B-block (T31/C100) power-up + route + volume. */
static void jz_codec_b_powerup(struct jz_codec_priv *p)
{
	u32 value = 0;
	int i;

	codec_set(p, B_CGR_00, 0, 1, 0x0);
	mdelay(10);
	codec_set(p, B_CGR_00, 0, 1, 0x3);
	mdelay(10);

	codec_set(p, B_CACR2_03, 0, 7, 0x3e);	/* DAC I2S MASTER */
	codec_set(p, B_CDCR1_04, 0, 7, 0x10);	/* DAC iface = I2S */
	codec_set(p, B_CDCR2_05, 0, 7, 0x3e);
	codec_set(p, B_CANACR_26, 0, 1, 0x1);
	mdelay(10);
	codec_set(p, B_CCR_21, 0, 6, 0x1);
	mdelay(10);

	codec_set(p, B_CAACR_22, 5, 5, 1);	/* reference voltage */
	mdelay(10);
	for (i = 0; i <= 6; i++) {
		value |= value << 1 | 1;
		codec_set(p, B_CCR_21, 0, 6, value);
		mdelay(30);
	}
	mdelay(20);

	/*
	 * Speaker route (vendor T31/C100 codec_set_speaker): power
	 * trim, mute mic, walk the analog driver chain step-by-step
	 * (current source -> reference voltage -> POP control ->
	 * CHR_28 step 5/6 -> CANACR2_27 bits 7..4 in order -> CHR_28
	 * step 7 unmute), then write replay volume in CHR_28[0:4],
	 * then drop CCR_21 to low-power. Per-bit ordering and the
	 * 10 ms settling delays are analog requirements - blanket
	 * writes here will keep the output muted.
	 */
	codec_set(p, B_POWER_20, 0, 2, 0x7);
	codec_set(p, B_CMICCR_23, 0, 3, 0xf);

	codec_set(p, B_CANACR_26, 3, 3, 1);
	mdelay(10);
	codec_set(p, B_CANACR_26, 2, 2, 1);
	mdelay(10);
	codec_set(p, B_CANACR_26, 0, 1, 0x2);
	mdelay(10);

	codec_set(p, B_CHR_28, 5, 5, 1);
	mdelay(10);
	codec_set(p, B_CHR_28, 6, 6, 1);
	mdelay(10);

	codec_set(p, B_CANACR2_27, 7, 7, 1);
	mdelay(10);
	codec_set(p, B_CANACR2_27, 6, 6, 1);
	mdelay(10);
	codec_set(p, B_CANACR2_27, 5, 5, 1);
	mdelay(10);
	codec_set(p, B_CANACR2_27, 4, 4, 1);
	mdelay(10);
	codec_set(p, B_CHR_28, 7, 7, 1);
	mdelay(10);

	codec_set(p, B_CHR_28, 0, 4, p->volume);
	mdelay(10);

	codec_set(p, B_CCR_21, 0, 6, 0x1);
	mdelay(10);
}

static int jz_codec_set_params(struct udevice *dev, int interface,
				int rate, int mclk_freq,
				int bits_per_sample, uint channels)
{
	struct jz_codec_priv *p = dev_get_priv(dev);

	/* The codec island shares the AIC gate; ungate defensively
	 * (probe order may put the codec before the i2s device). */
	clrbits_le32((void __iomem *)(CPM_BASE + CPM_CLKGR0),
		     CPM_CLKGR0_AIC);

	if (p->soc == JZ_CODEC_T31_C100) {
		jz_codec_b_powerup(p);
		codec_set(p, B_CSRR_44, 0, 2, jz_codec_fs(rate));
	} else {
		jz_codec_a_powerup(p);
		codec_set(p, A_CSRR_44, 0, 2, jz_codec_fs(rate));
	}

	/* Amp on LAST (anti-pop) - raw GPIO at the verified 0x100 stride. */
	jz_codec_spk_drive(p, true);

	return 0;
}

static int jz_codec_probe(struct udevice *dev)
{
	struct jz_codec_priv *p = dev_get_priv(dev);
	struct ofnode_phandle_args args;
	fdt_addr_t addr;
	u32 v;

	p->soc = (enum jz_codec_soc)dev_get_driver_data(dev);

	addr = dev_read_addr(dev);
	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;
	p->base = map_physmem(addr, 0x140, MAP_NOCACHE);

	/* Replay volume: DT prop, default 0x18, clamped to [0, 0x1f]. */
	p->volume = dev_read_u32_default(dev, "ingenic,replay-volume",
					 0x18) & 0x1f;

	/* Speaker-amp GPIO: parse <&gpx pin flags> ourselves so we can
	 * drive the pin raw (DM gpio path does not de-mux on this IP).
	 * Port index = the bank's `reg` (0=PA 1=PB 2=PC 3=PD); flags
	 * bit 0 = GPIO_ACTIVE_LOW (per dt-bindings/gpio/gpio.h). */
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

static const struct audio_codec_ops jz_codec_ops = {
	.set_params	= jz_codec_set_params,
};

static const struct udevice_id jz_codec_ids[] = {
	{ .compatible = "ingenic,t20-codec",  .data = JZ_CODEC_T20 },
	{ .compatible = "ingenic,t10-codec",  .data = JZ_CODEC_T10 },
	{ .compatible = "ingenic,t21-codec",  .data = JZ_CODEC_T30_T21 },
	{ .compatible = "ingenic,t23-codec",  .data = JZ_CODEC_T31_C100 },
	{ .compatible = "ingenic,t30-codec",  .data = JZ_CODEC_T30_T21 },
	{ .compatible = "ingenic,t31-codec",  .data = JZ_CODEC_T31_C100 },
	{ .compatible = "ingenic,c100-codec", .data = JZ_CODEC_T31_C100 },
	{ }
};

U_BOOT_DRIVER(jz_t10_codec) = {
	.name		= "jz_t10_codec",
	.id		= UCLASS_AUDIO_CODEC,
	.of_match	= jz_codec_ids,
	.probe		= jz_codec_probe,
	.ops		= &jz_codec_ops,
	.priv_auto	= sizeof(struct jz_codec_priv),
};
