// SPDX-License-Identifier: GPL-2.0+
/*
 * Maxim Integrated DS1807 digital potentiometer driver
 * Copyright (c) 2018 Michael Trimarchi
 *
 * Datasheet: https://datasheets.maximintegrated.com/en/ds/DS1807.pdf
 *
 * DEVID	#Wipers	#Positions	Resistor Opts (kOhm)	i2c address
 * ds1807	2	65		45,			0101xxx
 *
 */

#include <linux/err.h>
#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/of.h>

#define DS1807_MAX_STEP		64
#define DS1807_MUTE		-90
#define DS1807_WRITE(chan)	(0xa8 | ((chan) + 1))

#define DS1807_CHANNEL(ch) {					\
	.type = IIO_CHAN_INFO_HARDWAREGAIN,			\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (ch),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_HARDWAREGAIN),	\
}

static const struct iio_chan_spec ds1807_channels[] = {
	DS1807_CHANNEL(0),
	DS1807_CHANNEL(1),
};

struct ds1807_data {
	struct i2c_client *client;
};

static int ds1807_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct ds1807_data *data = iio_priv(indio_dev);
	int pot = chan->channel;
	int ret;
	u8 result[ARRAY_SIZE(ds1807_channels)];

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		ret = i2c_master_recv(data->client, result,
				indio_dev->num_channels);
		if (ret < 0)
			return ret;

		*val2 = 0;
		if (result[pot] == DS1807_MAX_STEP)
			*val = DS1807_MUTE;
		else
			*val = -result[pot];

                return IIO_VAL_INT_PLUS_MICRO_DB;
	}

	return -EINVAL;
}

static int ds1807_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct ds1807_data *data = iio_priv(indio_dev);
	int pot = chan->channel;

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		if (val2 !=0 || val < DS1807_MUTE || val > 0)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	val = (val == DS1807_MUTE) ? DS1807_MAX_STEP : -val;

	return i2c_smbus_write_byte_data(data->client, DS1807_WRITE(pot), val);
}

static const struct iio_info ds1807_info = {
	.read_raw = ds1807_read_raw,
	.write_raw = ds1807_write_raw,
};

static int ds1807_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ds1807_data *data;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	i2c_set_clientdata(client, indio_dev);

	data = iio_priv(indio_dev);
	data->client = client;

	indio_dev->dev.parent = dev;
	indio_dev->info = &ds1807_info;
	indio_dev->channels = ds1807_channels;
	indio_dev->num_channels = ARRAY_SIZE(ds1807_channels);
	indio_dev->name = client->name;

	return devm_iio_device_register(dev, indio_dev);
}

#if defined(CONFIG_OF)
static const struct of_device_id ds1807_dt_ids[] = {
	{ .compatible = "maxim,ds1807", },
	{}
};
MODULE_DEVICE_TABLE(of, ds1807_dt_ids);
#endif /* CONFIG_OF */

static const struct i2c_device_id ds1807_id[] = {
	{ "ds1807", },
	{}
};
MODULE_DEVICE_TABLE(i2c, ds1807_id);

static struct i2c_driver ds1807_driver = {
	.driver = {
		.name	= "ds1807",
		.of_match_table = of_match_ptr(ds1807_dt_ids),
	},
	.probe		= ds1807_probe,
	.id_table	= ds1807_id,
};

module_i2c_driver(ds1807_driver);

MODULE_AUTHOR("Michael Trimarchi <michael@amarulasolutions.com>");
MODULE_DESCRIPTION("DS1807 digital potentiometer");
MODULE_LICENSE("GPL v2");
