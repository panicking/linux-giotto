/*
 * PCM179X ASoC codec driver
 *
 * Copyright (c) Amarula Solutions B.V. 2013
 *
 *     Michael Trimarchi <michael@amarulasolutions.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/of.h>

#include "pcm179x.h"

#define PCM179X_DAC_VOL_LEFT	0x10
#define PCM179X_DAC_VOL_RIGHT	0x11
#define PCM179X_FMT_CONTROL	0x12
#define PCM179X_MODE_CONTROL	0x13
#define PCM179X_CONF_CONTROL	0x14
#define PCM179X_SOFT_MUTE	PCM179X_FMT_CONTROL

#define PCM179X_FMT_MASK	0x70
#define PCM179X_FMT_SHIFT	4
#define PCM179X_MUTE_MASK	0x01
#define PCM179X_MUTE_SHIFT	0
#define PCM179X_ATLD_ENABLE	(1 << 7)
#define PCM179X_DSD_ENABLE	(1 << 5)
#define PCM179x_CODEC_RST	(1 << 6)
#define PCM179X_DSD_FILTER(X)	(((X) & 0x3) << 2)


#define CLK2		(1 << 0)
#define CLK1		(1 << 1)
#define CLK0		(1 << 2)
#define W32		(1 << 3)
#define DSD_EN		(1 << 4)
#define SPDIF_IN	(1 << 5)
#define SPDIF_SEL	(1 << 6)

#define	DACMAX_SPEED_MAX	0xff
#define DACMAX_CLOCK		0x20

static const struct reg_default pcm179x_reg_defaults[] = {
	{ 0x10, 0xff },
	{ 0x11, 0xff },
	{ 0x12, 0x50 },
	{ 0x13, 0x00 },
	{ 0x14, 0x00 },
	{ 0x15, 0x01 },
	{ 0x16, 0x00 },
	{ 0x17, 0x00 },
	{ 0x20, 0x00 },
};

static bool pcm179x_accessible_reg(struct device *dev, unsigned int reg)
{
	return (reg >= 0x10 && reg <= 0x17) || reg == 0x20;
}

static bool pcm179x_writeable_reg(struct device *dev, unsigned register reg)
{
	bool accessible;

	accessible = pcm179x_accessible_reg(dev, reg);

	return accessible && reg != 0x16 && reg != 0x17;
}

enum pcm179x_type {
	PCM1792A = 1,
	PCM1795,
	PCM1796,
};

struct pcm179x_private {
	struct regmap *regmap;
	unsigned int format;
	unsigned int rate;
	unsigned int dsd_mode;
	unsigned int is_mute;
	u8 dacmax_register;
	enum pcm179x_type codec_model;
};

static int pcm179x_startup(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm179x_private *priv = snd_soc_codec_get_drvdata(codec);
	u64 formats = PCM1795_FORMATS;

	switch (priv->codec_model) {
	case PCM1792A:
		formats = PCM1792A_FORMATS;
		break;
	default:
		break;
	}

	if (formats != PCM1795_FORMATS)
		snd_pcm_hw_constraint_mask64(substream->runtime,
					     SNDRV_PCM_HW_PARAM_FORMAT, formats);

	msleep(50);
	return 0;
}

static int pcm179x_set_dai_fmt(struct snd_soc_dai *codec_dai,
			       unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct pcm179x_private *priv = snd_soc_codec_get_drvdata(codec);

	priv->format = format;

	return 0;
}

static int pcm179x_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm179x_private *priv = snd_soc_codec_get_drvdata(codec);
	int spdif_enable = !!(priv->dacmax_register & SPDIF_IN);
	int ret;

	pr_info("%s: mute %d dsd %d\n", __func__, mute, priv->dsd_mode);

	priv->is_mute = mute;

	if (spdif_enable && mute)
		return 0;

	if (priv->dsd_mode && mute) {
		ret = regmap_update_bits(priv->regmap, PCM179X_CONF_CONTROL,
					 PCM179X_DSD_ENABLE, 0);

		if (ret < 0)
			return ret;
	}

	ret = regmap_update_bits(priv->regmap, PCM179X_SOFT_MUTE,
				 PCM179X_MUTE_MASK, !!mute);
	if (ret < 0)
		return ret;

	return 0;
}

static int pcm179x_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm179x_private *priv = snd_soc_codec_get_drvdata(codec);
	unsigned int val = 0, ret;
	unsigned int dsd = 0;
	unsigned int mask = PCM179X_FMT_MASK | PCM179X_ATLD_ENABLE;

	priv->rate = params_rate(params);
	switch (priv->rate) {
	case 44100:
		break;
	case 48000:
		val |= CLK0;
		break;
	case 88200:
		val |= CLK1;
		break;
	case 96000:
		val |= (CLK1 | CLK0);
		break;
	case 176400:
		val |= (CLK2 | CLK1);
		break;
	case 192000:
		val |= (CLK2 | CLK0 | CLK1);
		break;
	case 352800:
		val |= CLK2;
	case 705600:
		val |= CLK1;

		/* This rate works only for DSD format */
		if (params_format(params) !=
		    SNDRV_PCM_FORMAT_DSD_U16_LE)
			return -EINVAL;

		val |= W32;
		break;
	default:
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_DSD_U16_LE:
		val |= DSD_EN;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		val |= W32;
		break;
	}

	priv->dacmax_register &= (SPDIF_IN | SPDIF_SEL);
	priv->dacmax_register |=  val;
	ret = regmap_update_bits(priv->regmap, DACMAX_CLOCK,
				 0xff, priv->dacmax_register);
	if (ret < 0)
		return ret;

	switch (priv->format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_width(params)) {
		case 32:
			val = 1;
			break;
		case 24:
			val = 2;
			break;
		case 16:
			val = 0;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
		switch (params_width(params)) {
		case 32:
			val = 4;
			break;
		case 24:
			val = 5;
			break;
		case 16:
			val = 4;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		dev_err(codec->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	val = val << PCM179X_FMT_SHIFT | PCM179X_ATLD_ENABLE;

	mask |= PCM179X_DSD_FILTER(3);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_DSD_U16_LE:
		dsd = PCM179X_DSD_ENABLE;
		val = PCM179X_DSD_FILTER(2);
		priv->dsd_mode = 1;
		break;
	default:
		priv->dsd_mode = 0;
	}

	pr_debug("%s: dsd enable %d\n", __func__, priv->dsd_mode);

	ret = regmap_update_bits(priv->regmap, PCM179X_FMT_CONTROL,
				 mask, val);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(priv->regmap, PCM179X_CONF_CONTROL,
				 PCM179X_DSD_ENABLE, dsd);

	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_dai_ops pcm179x_dai_ops = {
	.startup	= pcm179x_startup,
	.set_fmt	= pcm179x_set_dai_fmt,
	.hw_params	= pcm179x_hw_params,
	.digital_mute	= pcm179x_digital_mute,
};

static int spdif_get_input(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
        struct pcm179x_private *priv = snd_soc_component_get_drvdata(c);

	ucontrol->value.integer.value[0] = !!(priv->dacmax_register & SPDIF_SEL);
	return 0;
}

static int spdif_put_input(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
        struct pcm179x_private *priv = snd_soc_component_get_drvdata(c);
	int saved_value = !!(priv->dacmax_register & SPDIF_SEL);

	if (saved_value == ucontrol->value.integer.value[0])
		return 0;

	if (ucontrol->value.integer.value[0])
		priv->dacmax_register |= SPDIF_SEL;
	else
		priv->dacmax_register &= ~SPDIF_SEL;

	return 1;
}


static int spdif_switch_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
        struct pcm179x_private *priv = snd_soc_component_get_drvdata(c);

	ucontrol->value.integer.value[0] = !!(priv->dacmax_register & SPDIF_IN);
	return 0;
}

static int spdif_switch_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
        struct pcm179x_private *priv = snd_soc_component_get_drvdata(c);
	int saved_value = !!(priv->dacmax_register & SPDIF_IN);

	if (saved_value == ucontrol->value.integer.value[0])
		return 0;

	if (ucontrol->value.integer.value[0])
		priv->dacmax_register |= SPDIF_IN;
	else
		priv->dacmax_register &= ~SPDIF_IN;

	if (priv->dsd_mode)
		regmap_update_bits(priv->regmap, PCM179X_CONF_CONTROL,
				 PCM179X_DSD_ENABLE, !ucontrol->value.integer.value[0]);

	regmap_update_bits(priv->regmap, DACMAX_CLOCK,
			 0xff, priv->dacmax_register);

	regmap_update_bits(priv->regmap, PCM179X_SOFT_MUTE,
			 PCM179X_MUTE_MASK, priv->is_mute & (!saved_value));
	return 1;
}

static const DECLARE_TLV_DB_SCALE(pcm179x_dac_tlv, -12000, 50, 1);

static const struct snd_kcontrol_new pcm179x_controls[] = {
	SOC_DOUBLE_R_RANGE_TLV("DAC Playback Volume", PCM179X_DAC_VOL_LEFT,
			 PCM179X_DAC_VOL_RIGHT, 0, 0xf, 0xff, 0,
			 pcm179x_dac_tlv),
	SOC_SINGLE("DAC Invert Output Switch", PCM179X_MODE_CONTROL, 7, 1, 0),
	SOC_SINGLE("DAC Rolloff Filter Switch", PCM179X_MODE_CONTROL, 1, 1, 0),
	/* SPDIF control */
	SOC_SINGLE_BOOL_EXT("SPDIF Input Switch", 0,
			spdif_switch_get, spdif_switch_put),
	SOC_SINGLE_BOOL_EXT("SPDIF Select Switch", 0,
			spdif_get_input, spdif_put_input),
};

static const struct snd_soc_dapm_widget pcm179x_dapm_widgets[] = {
SND_SOC_DAPM_OUTPUT("IOUTL+"),
SND_SOC_DAPM_OUTPUT("IOUTL-"),
SND_SOC_DAPM_OUTPUT("IOUTR+"),
SND_SOC_DAPM_OUTPUT("IOUTR-"),
};

static const struct snd_soc_dapm_route pcm179x_dapm_routes[] = {
	{ "IOUTL+", NULL, "Playback" },
	{ "IOUTL-", NULL, "Playback" },
	{ "IOUTR+", NULL, "Playback" },
	{ "IOUTR-", NULL, "Playback" },
};

static struct snd_soc_dai_driver pcm179x_dai = {
	.name = "pcm179x-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 10000,
		.rate_max = 705600,
		.formats = PCM1795_FORMATS, },
	.ops = &pcm179x_dai_ops,
};

const struct regmap_config pcm179x_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= 32,
	.reg_defaults		= pcm179x_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(pcm179x_reg_defaults),
	.writeable_reg		= pcm179x_writeable_reg,
	.readable_reg		= pcm179x_accessible_reg,
};
EXPORT_SYMBOL_GPL(pcm179x_regmap_config);

static const struct snd_soc_codec_driver soc_codec_dev_pcm179x = {
	.component_driver = {
		.controls		= pcm179x_controls,
		.num_controls		= ARRAY_SIZE(pcm179x_controls),
		.dapm_widgets		= pcm179x_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(pcm179x_dapm_widgets),
		.dapm_routes		= pcm179x_dapm_routes,
		.num_dapm_routes	= ARRAY_SIZE(pcm179x_dapm_routes),
	}
};

const struct of_device_id pcm179x_of_match[] = {
	{ .compatible = "ti,pcm1792a", .data = (void *)PCM1792A },
	{ .compatible = "ti,pcm1795", .data = (void *)PCM1795 },
	{ .compatible = "ti,pcm1796", .data = (void *)PCM1792A },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm179x_of_match);
EXPORT_SYMBOL_GPL(pcm179x_of_match);

int pcm179x_common_init(struct device *dev, struct regmap *regmap)
{
	struct pcm179x_private *pcm179x;
	struct device_node *np = dev->of_node;
	enum pcm179x_type codec_model = PCM1795;

	pcm179x = devm_kzalloc(dev, sizeof(struct pcm179x_private),
				GFP_KERNEL);
	if (!pcm179x)
		return -ENOMEM;

	if (np) {
		const struct of_device_id *of_id;

		of_id = of_match_device(pcm179x_of_match, dev);
		if (of_id)
			codec_model = (enum pcm179x_type) of_id->data;
	}

	if (codec_model)
		pcm179x->codec_model =  codec_model;

	pcm179x->regmap = regmap;
	pcm179x->is_mute = 1;
	dev_set_drvdata(dev, pcm179x);

	return snd_soc_register_codec(dev,
			&soc_codec_dev_pcm179x, &pcm179x_dai, 1);
}
EXPORT_SYMBOL_GPL(pcm179x_common_init);

int pcm179x_common_exit(struct device *dev)
{
	snd_soc_unregister_codec(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(pcm179x_common_exit);

MODULE_DESCRIPTION("ASoC PCM179X driver");
MODULE_AUTHOR("Michael Trimarchi <michael@amarulasolutions.com>");
MODULE_LICENSE("GPL");
