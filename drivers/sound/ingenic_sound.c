// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst1 sound glue (UCLASS_SOUND).
 *
 * Ties the AIC i2s device (cpu) and the on-chip codec together for
 * U-Boot's `sound` command. Mirrors rockchip_sound: setup() programs
 * the codec from the i2s stream params, play() feeds PCM via the i2s
 * TX (PIO). The i2s driver self-handles the AIC clock; the on-chip
 * codec needs no external pinmux, so there is no clk/pinctrl here.
 */

#define LOG_CATEGORY UCLASS_SOUND

#include <audio_codec.h>
#include <dm.h>
#include <i2s.h>
#include <log.h>
#include <sound.h>

static int ingenic_sound_setup(struct udevice *dev)
{
	struct sound_uc_priv *uc_priv = dev_get_uclass_priv(dev);
	struct i2s_uc_priv *i2s = dev_get_uclass_priv(uc_priv->i2s);
	int ret;

	if (uc_priv->setup_done)
		return -EALREADY;

	ret = audio_codec_set_params(uc_priv->codec, i2s->id,
				     i2s->samplingrate,
				     i2s->samplingrate * i2s->rfs,
				     i2s->bitspersample, i2s->channels);
	if (ret)
		return ret;

	uc_priv->setup_done = true;
	return 0;
}

static int ingenic_sound_play(struct udevice *dev, void *data, uint data_size)
{
	struct sound_uc_priv *uc_priv = dev_get_uclass_priv(dev);

	return i2s_tx_data(uc_priv->i2s, data, data_size);
}

/*
 * sound_stop_play() is always called by sound_beep() after the play
 * loop, and U-Boot's sound-uclass.c invokes ops->stop_play()
 * unconditionally (its guard checks ops->play, not ops->stop_play).
 * Forward to the i2s driver, which keeps AICCR_ERPL held high across
 * the per-buffer tx_data calls (toggling it between buffers is
 * audible on B-block codecs as silence gaps) and only drops it here.
 */
int ingenic_i2s_stop_play(struct udevice *dev);

static int ingenic_sound_stop_play(struct udevice *dev)
{
	struct sound_uc_priv *uc_priv = dev_get_uclass_priv(dev);

	return ingenic_i2s_stop_play(uc_priv->i2s);
}

static int ingenic_sound_probe(struct udevice *dev)
{
	return sound_find_codec_i2s(dev);
}

static const struct sound_ops ingenic_sound_ops = {
	.setup		= ingenic_sound_setup,
	.play		= ingenic_sound_play,
	.stop_play	= ingenic_sound_stop_play,
};

static const struct udevice_id ingenic_sound_ids[] = {
	{ .compatible = "ingenic,t10-sound" },
	{ .compatible = "ingenic,t20-sound" },
	{ .compatible = "ingenic,t21-sound" },
	{ .compatible = "ingenic,t23-sound" },
	{ .compatible = "ingenic,t30-sound" },
	{ .compatible = "ingenic,t31-sound" },
	{ .compatible = "ingenic,c100-sound" },
	{ }
};

U_BOOT_DRIVER(ingenic_sound) = {
	.name		= "ingenic_sound",
	.id		= UCLASS_SOUND,
	.of_match	= ingenic_sound_ids,
	.probe		= ingenic_sound_probe,
	.ops		= &ingenic_sound_ops,
};
