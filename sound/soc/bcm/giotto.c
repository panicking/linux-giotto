/*
 * giotto.c  --  SoC audio for pandaboard demokit
 *
 * Author: Michael Trimarchi <michael@amarulasolutions.com>
 *
 * Based on:
 * Author: Misael Lopez Cruz <x0052729@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "../codecs/pcm179x.h"

#define CLK2	(1 << 0)
#define CLK1	(1 << 1)
#define CLK0	(1 << 2)
#define W32	(1 << 3)
#define DSD_EN	(1 << 4)

#define DAI_NAME_SIZE	32

struct giotto_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
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
		/* This rate works only for DSD format */
		if (params_format(params) !=
		    SNDRV_PCM_FORMAT_DSD_U16_LE)
			return -EINVAL;

		mask |= (CLK2 | CLK1);
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
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	return giotto_ext_clock_update(data, params, cpu_dai);
}

static struct snd_soc_ops giotto_ops = {
	.hw_params = giotto_hw_params,
};

static int giotto_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	pr_info("%s: INIT\n", __func__);
	return 0;
}

static int giotto_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *i2s_np;
	struct platform_device *i2s_pdev;
	struct giotto_data *data;
	int nreset;
	int ret;


	nreset = of_get_named_gpio(np, "nreset", 0);

	if (!gpio_is_valid(nreset)) {
		dev_err(&pdev->dev, "incorrect giotto gpios (%d)\n",
				nreset);
		return -EINVAL;
	}

	dev_info(&pdev->dev, "Initialize codec chip\n");
	devm_gpio_request_one(&pdev->dev, nreset, GPIOF_OUT_INIT_HIGH,
				"nreset");
	msleep(20);

	i2s_np = of_parse_phandle(pdev->dev.of_node, "i2s-controller", 0);
	if (!i2s_np) {
		dev_err(&pdev->dev, "phandle missing or invalid for i2s-controller\n");
		ret = -EPROBE_DEFER;
		goto fail;
	}

	i2s_pdev = of_find_device_by_node(i2s_np);
	if (!i2s_pdev) {
		dev_err(&pdev->dev, "failed to find i2s platform device\n");
		ret = -EPROBE_DEFER;
		goto fail;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	data->dai.name = "GIOTTO-I2S";
	data->dai.stream_name = "GIOTTO-Audio";
	data->dai.codec_dai_name = "pcm179x-hifi";
	data->dai.codec_name = "spi0.0";
	data->dai.cpu_of_node = i2s_np;
	data->dai.platform_of_node = i2s_np;
	data->dai.init = &giotto_dai_init;
	data->dai.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			    SND_SOC_DAIFMT_CBM_CFM;
	data->dai.ops = &giotto_ops;
	data->card.dapm_widgets = giotto_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(giotto_dapm_widgets);

	data->card.name = "Giotto Dac";
	data->card.dev = &pdev->dev;
	data->card.owner = THIS_MODULE;
	data->card.dai_link = &data->dai;
	data->card.num_links = 1;

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

	platform_set_drvdata(pdev, data);
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

MODULE_AUTHOR("Michael Trimarchi <michael@amurulasolutions.com>");
MODULE_DESCRIPTION("ALSA SoC GIOTTO");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, giotto_dt_ids);

