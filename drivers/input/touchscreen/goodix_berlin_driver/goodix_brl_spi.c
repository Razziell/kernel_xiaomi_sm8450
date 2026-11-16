 /*
  * Goodix Touchscreen Driver
  * Copyright (C) 2020 - 2021 Goodix, Inc.
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation; either version 2 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be a reference
  * to you, when you are integrating the GOODiX's CTP IC into your system,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  * General Public License for more details.
  *
  */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include "goodix_ts_core.h"
#define TS_DRIVER_NAME		"gtx8_spi"

#define SPI_TRANS_PREFIX_LEN    1
#define REGISTER_WIDTH          4
#define SPI_READ_DUMMY_LEN      4
#define SPI_READ_PREFIX_LEN  \
		(SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH + SPI_READ_DUMMY_LEN)
#define SPI_WRITE_PREFIX_LEN (SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH)

#define SPI_WRITE_FLAG  0xF0
#define SPI_READ_FLAG   0xF1

static struct platform_device *goodix_pdev;
struct goodix_bus_interface goodix_spi_bus;

/*
 * Transfers up to this size use two buffers allocated once at probe instead
 * of a pair of allocations per transfer. It covers a full touch report (10
 * points), the commands and their acknowledge. Larger transfers (firmware
 * update, raw data, config) are rare and still allocate.
 */
#define GOODIX_SPI_BUF_LEN	256

static DEFINE_MUTEX(goodix_spi_buf_lock);
static u8 *goodix_spi_tx_buf;
static u8 *goodix_spi_rx_buf;

/*
 * goodix_spi_get_bufs - get zeroed tx and (if @rx_buf) rx buffers for a
 * transfer of @len bytes. On success goodix_spi_put_bufs() must be called
 * with the same arguments; the preallocated buffers are locked until then.
 */
static int goodix_spi_get_bufs(unsigned int len, u8 **tx_buf, u8 **rx_buf)
{
	if (len <= GOODIX_SPI_BUF_LEN) {
		mutex_lock(&goodix_spi_buf_lock);
		memset(goodix_spi_tx_buf, 0, len);
		*tx_buf = goodix_spi_tx_buf;
		if (rx_buf)
			*rx_buf = goodix_spi_rx_buf;
		return 0;
	}

	*tx_buf = kzalloc(len, GFP_KERNEL);
	if (rx_buf)
		*rx_buf = kzalloc(len, GFP_KERNEL);
	if (!*tx_buf || (rx_buf && !*rx_buf)) {
		ts_err("alloc tx/rx_buf failed, size:%d", len);
		kfree(*tx_buf);
		if (rx_buf)
			kfree(*rx_buf);
		return -ENOMEM;
	}

	return 0;
}

static void goodix_spi_put_bufs(unsigned int len, u8 *tx_buf, u8 *rx_buf)
{
	if (len <= GOODIX_SPI_BUF_LEN) {
		mutex_unlock(&goodix_spi_buf_lock);
		return;
	}

	kfree(tx_buf);
	kfree(rx_buf);
}

/*
 * goodix_spi_do_read - read device register through spi bus
 * @prefix_len: bytes clocked before the data: flag, address and dummy bytes
 */
static int goodix_spi_do_read(struct device *dev, unsigned int addr,
	unsigned char *data, unsigned int len, unsigned int prefix_len)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned int xfer_len = prefix_len + len;
	u8 *rx_buf = NULL;
	u8 *tx_buf = NULL;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	int ret;

	ret = goodix_spi_get_bufs(xfer_len, &tx_buf, &rx_buf);
	if (ret)
		return ret;

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	/*spi_read tx_buf format: 0xF1 + addr(4bytes) + dummy + data*/
	tx_buf[0] = SPI_READ_FLAG;
	tx_buf[1] = (addr >> 24) & 0xFF;
	tx_buf[2] = (addr >> 16) & 0xFF;
	tx_buf[3] = (addr >> 8) & 0xFF;
	tx_buf[4] = addr & 0xFF;
	memset(&tx_buf[SPI_WRITE_PREFIX_LEN], 0xFF,
	       prefix_len - SPI_WRITE_PREFIX_LEN);

	xfers.tx_buf = tx_buf;
	xfers.rx_buf = rx_buf;
	xfers.len = xfer_len;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);
	ret = spi_sync(spi, &spi_msg);
	if (ret < 0)
		ts_err("spi transfer error:%d", ret);
	else
		memcpy(data, &rx_buf[prefix_len], len);

	goodix_spi_put_bufs(xfer_len, tx_buf, rx_buf);
	return ret;
}

/**
 * goodix_spi_read_bra- read device register through spi bus
 * @dev: pointer to device data
 * @addr: register address
 * @data: read buffer
 * @len: bytes to read
 * return: 0 - read ok, < 0 - spi transter error
 */
static int goodix_spi_read_bra(struct device *dev, unsigned int addr,
	unsigned char *data, unsigned int len)
{
	return goodix_spi_do_read(dev, addr, data, len, SPI_READ_PREFIX_LEN);
}

/* Berlin B and D take one dummy byte less than Berlin A */
static int goodix_spi_read(struct device *dev, unsigned int addr,
	unsigned char *data, unsigned int len)
{
	return goodix_spi_do_read(dev, addr, data, len,
				  SPI_READ_PREFIX_LEN - 1);
}

/**
 * goodix_spi_write- write device register through spi bus
 * @dev: pointer to device data
 * @addr: register address
 * @data: write buffer
 * @len: bytes to write
 * return: 0 - write ok; < 0 - spi transter error.
 */
static int goodix_spi_write(struct device *dev, unsigned int addr,
		unsigned char *data, unsigned int len)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned int xfer_len = SPI_WRITE_PREFIX_LEN + len;
	u8 *tx_buf = NULL;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	int ret;

	ret = goodix_spi_get_bufs(xfer_len, &tx_buf, NULL);
	if (ret)
		return ret;

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	tx_buf[0] = SPI_WRITE_FLAG;
	tx_buf[1] = (addr >> 24) & 0xFF;
	tx_buf[2] = (addr >> 16) & 0xFF;
	tx_buf[3] = (addr >> 8) & 0xFF;
	tx_buf[4] = addr & 0xFF;
	memcpy(&tx_buf[SPI_WRITE_PREFIX_LEN], data, len);
	xfers.tx_buf = tx_buf;
	xfers.len = xfer_len;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);
	ret = spi_sync(spi, &spi_msg);
	if (ret < 0)
		ts_err("spi transfer error:%d", ret);

	goodix_spi_put_bufs(xfer_len, tx_buf, NULL);
	return ret;
}

static void goodix_pdev_release(struct device *dev)
{
	ts_info("goodix pdev released");
	kfree(goodix_pdev);
}

#ifdef CONFIG_OF
static const struct of_device_id spi_matchs[] = {
	{.compatible = "goodix,gt9897S",},
	{.compatible = "goodix,gt9897T",},
	{.compatible = "goodix,gt9966S",},
	{.compatible = "goodix,gt9916S",},
	{},
};
#endif

static int goodix_spi_probe(struct spi_device *spi)
{
	int ret = 0;

	ts_info("goodix spi probe in");

	ret = goodix_check_ts_id_gpio(&spi->dev);
	if (ret)
		return ret;

	/* init spi_device */
	spi->mode          = SPI_MODE_0;
	spi->bits_per_word = 8;

	ret = spi_setup(spi);
	if (ret) {
		ts_err("failed set spi mode, %d", ret);
		return ret;
	}

	/* own allocations: usable for DMA, unlike the stack or static data */
	goodix_spi_tx_buf = devm_kzalloc(&spi->dev, GOODIX_SPI_BUF_LEN,
					 GFP_KERNEL);
	goodix_spi_rx_buf = devm_kzalloc(&spi->dev, GOODIX_SPI_BUF_LEN,
					 GFP_KERNEL);
	if (!goodix_spi_tx_buf || !goodix_spi_rx_buf)
		return -ENOMEM;

	/* get ic type */
	ret = goodix_get_ic_type(spi->dev.of_node, spi_matchs);
	if (ret < 0)
		return ret;

	goodix_spi_bus.ic_type = ret;
	goodix_spi_bus.bus_type = GOODIX_BUS_TYPE_SPI;
	goodix_spi_bus.dev = &spi->dev;
	if (goodix_spi_bus.ic_type == IC_TYPE_BERLIN_A)
		goodix_spi_bus.read = goodix_spi_read_bra;
	else
		goodix_spi_bus.read = goodix_spi_read;
	goodix_spi_bus.write = goodix_spi_write;
	/* ts core device */
	goodix_pdev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
	if (!goodix_pdev)
		return -ENOMEM;

	goodix_pdev->name = GOODIX_CORE_DRIVER_NAME;
	goodix_pdev->id = 0;
	goodix_pdev->num_resources = 0;
	/*
	 * you can find this platform dev in
	 * /sys/devices/platfrom/goodix_ts.0
	 * goodix_pdev->dev.parent = &client->dev;
	 */
	goodix_pdev->dev.platform_data = &goodix_spi_bus;
	goodix_pdev->dev.release = goodix_pdev_release;

	/*
	 * register platform device, then the goodix_ts_core
	 * module will probe the touch deivce.
	 */
	ret = platform_device_register(goodix_pdev);
	if (ret) {
		ts_err("failed register goodix platform device, %d", ret);
		goto err_pdev;
	}
	ts_info("spi probe out");
	return 0;

err_pdev:
	kfree(goodix_pdev);
	goodix_pdev = NULL;
	ts_info("spi probe out, %d", ret);
	return ret;
}

static int goodix_spi_remove(struct spi_device *spi)
{
	platform_device_unregister(goodix_pdev);
	return 0;
}

static const struct spi_device_id spi_id_table[] = {
	{TS_DRIVER_NAME, 0},
	{},
};

static struct spi_driver goodix_spi_driver = {
	.driver = {
		.name = TS_DRIVER_NAME,
		//.owner = THIS_MODULE,
		.of_match_table = spi_matchs,
	},
	.id_table = spi_id_table,
	.probe = goodix_spi_probe,
	.remove = goodix_spi_remove,
};

int goodix_spi_bus_init(void)
{
	ts_info("Goodix spi driver init");
	return spi_register_driver(&goodix_spi_driver);
}

void goodix_spi_bus_exit(void)
{
	ts_info("Goodix spi driver exit");
	spi_unregister_driver(&goodix_spi_driver);
}
