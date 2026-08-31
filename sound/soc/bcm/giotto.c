// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * giotto.c  --  SoC audio for Giotto DAC board
 *
 * Author: Michael Trimarchi <michael@amarulasolutions.com>
 *
 * Based on:
 * Author: Misael Lopez Cruz <x0052729@ti.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/consumer.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "../codecs/pcm179x.h"

#define CLK2	(1 << 0)
#define CLK1	(1 << 1)
#define CLK0	(1 << 2)
#define W32	(1 << 3)
#define DSD_EN	(1 << 4)

struct giotto_data {
	struct snd_soc_card card;

	/* IIO */
	struct iio_channel *iio_ch;
	int volume;
};

static const struct snd_soc_dapm_widget giotto_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Line Out", NULL),
};

static int giotto_ext_clock_update(struct giotto_data *data,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	u8 mask = 0;

	pr_debug("%s: format 0x%x\n", __func__,
		 params_format(params));

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_DSD_U16_LE:
		mask |= DSD_EN;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		mask |= W32;
		break;
	default:
		dev_err(dai->dev, "format not supported!\n");
		return -EINVAL;
	}

	switch (params_rate(params)) {
	case 44100:
		break;
	case 48000:
		mask |= CLK0;
		break;
	case 88200:
		mask |= CLK1;
		break;
	case 96000:
		mask |= (CLK1 | CLK0);
		break;
	case 176400:
		mask |= (CLK2 | CLK1);
		break;
	case 192000:
		mask |= (CLK2 | CLK0 | CLK1);
		break;
	case 352800:
		mask |= CLK2;
	case 705600:
		mask |= CLK1;
		/* These rates work only for DSD format */
		if (params_format(params) !=
		    SNDRV_PCM_FORMAT_DSD_U16_LE)
			return -EINVAL;

		mask |= W32;
		break;
	default:
		return -EINVAL;
	}

	pr_debug("%s: Set frequency %d mask 0x%x\n", __func__,
		 params_rate(params), mask);

	return 0;
}

static int giotto_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct giotto_data *data = container_of(rtd->card,
						struct giotto_data, card);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);

	return giotto_ext_clock_update(data, params, cpu_dai);
}

static const struct snd_soc_ops giotto_ops = {
	.hw_params = giotto_hw_params,
};

static int giotto_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	pr_info("%s: INIT\n", __func__);
	return 0;
}

static int volume_giotto_ctl_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 37;
	uinfo->value.integer.max = 100;
	return 0;
}

static int volume_giotto_ctl_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct giotto_data *data = snd_soc_card_get_drvdata(card);

	ucontrol->value.integer.value[0] = data->volume;

	return 0;
}

static int volume_giotto_ctl_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct giotto_data *data = snd_soc_card_get_drvdata(card);
	int val;
	int ret;

	val = (int)ucontrol->value.integer.value[0] - 100;

	ret = iio_write_channel_attribute(&data->iio_ch[0], val, 0,
					  IIO_CHAN_INFO_HARDWAREGAIN);
	if (ret < 0)
		return ret;

	ret = iio_write_channel_attribute(&data->iio_ch[1], val, 0,
					  IIO_CHAN_INFO_HARDWAREGAIN);
	if (ret < 0)
		return ret;

	data->volume = ucontrol->value.integer.value[0];

	return 0;
}

static struct snd_kcontrol_new giotto_ctl[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM Playback Volume",
		.info = volume_giotto_ctl_info,
		.get = volume_giotto_ctl_get,
		.put = volume_giotto_ctl_put,
	},
};

SND_SOC_DAILINK_DEFS(giotto,
	DAILINK_COMP_ARRAY(COMP_CPU("bcm2835-i2s.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("spi0.0", "pcm179x-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("bcm2835-i2s.0")));

static struct snd_soc_dai_link giotto_dai = {
	.name = "GIOTTO-I2S",
	.stream_name = "GIOTTO-Audio",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBM_CFM,
	.ops = &giotto_ops,
	.init = giotto_dai_init,
	SND_SOC_DAILINK_REG(giotto),
};

static int giotto_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *i2s_np;
	struct giotto_data *data;
	int nreset;
	int ret;

	nreset = of_get_named_gpio(np, "nreset", 0);

	if (!gpio_is_valid(nreset)) {
		dev_err(dev, "incorrect giotto gpios (%d)\n", nreset);
		return -EINVAL;
	}

	dev_info(dev, "Initialize codec chip\n");
	devm_gpio_request_one(dev, nreset, GPIOF_OUT_INIT_HIGH, "nreset");
	msleep(20);

	i2s_np = of_parse_phandle(np, "i2s-controller", 0);
	if (!i2s_np) {
		dev_err(dev, "phandle missing or invalid for i2s-controller\n");
		return -EPROBE_DEFER;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	data->card.name = "Giotto Dac";
	data->card.dev = dev;
	data->card.owner = THIS_MODULE;
	data->card.dai_link = &giotto_dai;
	data->card.num_links = 1;
	data->card.dapm_widgets = giotto_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(giotto_dapm_widgets);
	data->card.controls = giotto_ctl;
	data->card.num_controls = ARRAY_SIZE(giotto_ctl);
	snd_soc_card_set_drvdata(&data->card, data);

	giotto_dai.cpus->dai_name = NULL;
	giotto_dai.cpus->of_node = i2s_np;
	giotto_dai.platforms->name = NULL;
	giotto_dai.platforms->of_node = i2s_np;

	data->iio_ch = devm_iio_channel_get_all(dev);
	if (IS_ERR(data->iio_ch)) {
		dev_err(dev, "snd_soc_register_card iio_ch failed\n");
		ret = -EPROBE_DEFER;
		goto fail;
	}

	ret = devm_snd_soc_register_card(dev, &data->card);
	if (ret) {
		dev_err(dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

	of_node_put(i2s_np);

	return 0;

fail:
	if (i2s_np)
		of_node_put(i2s_np);

	return ret;
}

static int giotto_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id giotto_dt_ids[] = {
	{ .compatible = "bcm2708,bcm2708-audio-giotto", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, giotto_dt_ids);

static struct platform_driver giotto_driver = {
	.driver = {
		.name = "giotto",
		.owner = THIS_MODULE,
		.of_match_table = giotto_dt_ids,
	},
	.probe = giotto_probe,
	.remove = giotto_remove,
};
module_platform_driver(giotto_driver);

MODULE_AUTHOR("Michael Trimarchi <michael@amarulasolutions.com>");
MODULE_DESCRIPTION("ALSA SoC GIOTTO");
MODULE_LICENSE("GPL");
