/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ilitek ITK9881H SPI touch driver for Xiaomi Redmi 8 (olive)
 * Ported from Xiaomi msm-4.9 drivers/input/touchscreen/ITK9881H/
 * to mainline Linux (PostMarketOS 26.06).
 *
 * Reverse-engineered from Firmware/ boot.img DTB:
 *   compatible = "tchip,ilitek"
 *   bus        = spi@78b7000, cs = 0
 *   reset-gpio = tlmm 64 (GPIO 64)
 *   irq-gpio   = tlmm 65, level, rising edge  (flags 0x2001)
 *   spi-max-frequency = 10 MHz, SPI_MODE_0 (CPOL=0 CPHA=0) + cs-high per vendor DTS
 *   regulators = vdd / vcc @ 1.8 V
 *   panel = 720 x 1520, channels 18 x 32
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pm.h>

#include "ilitek_core.h"

#define ILITEK_NAME "ilitek_spi"

/* ---- low-level SPI helpers ---- */

static int ilitek_spi_transfer(struct ilitek_ts *ts,
			       const u8 *tx, u8 *rx, unsigned len)
{
	struct spi_message msg;
	struct spi_transfer xfer = {
		.tx_buf        = tx,
		.rx_buf        = rx,
		.len           = len,
		.speed_hz      = ILITEK_SPI_CLK_HZ,
	};

	if (!ts->spi)
		return -ENODEV;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	return spi_sync(ts->spi, &msg);
}

int ilitek_spi_write(struct ilitek_ts *ts, const u8 *buf, int len)
{
	int ret;

	if (!len)
		return -EINVAL;

	ret = ilitek_spi_transfer(ts, buf, NULL, len);
	if (ret < 0) {
		dev_err(ts->dev, "spi write error ret=%d\n", ret);
		return ret;
	}
	return len;
}

int ilitek_spi_read(struct ilitek_ts *ts, u8 *buf, int len)
{
	int ret;
	u8 dummy = SPI_READ;

	if (!len)
		return -EINVAL;

	ret = ilitek_spi_transfer(ts, &dummy, buf, len);
	if (ret < 0) {
		dev_err(ts->dev, "spi read error ret=%d\n", ret);
		return ret;
	}
	return len;
}

int ilitek_spi_write_then_read(struct ilitek_ts *ts,
			       const u8 *tx, unsigned n_tx,
			       u8 *rx, unsigned n_rx)
{
	int ret;
	u8 tmp_rx = 0;
	struct spi_message msg;
	struct spi_transfer xfers[2];
	int n = 0;

	if (!n_tx && !n_rx)
		return -EINVAL;

	memset(xfers, 0, sizeof(xfers));

	if (n_tx) {
		xfers[n].tx_buf = tx;
		xfers[n].len    = n_tx;
		xfers[n].rx_buf = n_rx ? &tmp_rx : NULL;
		xfers[n].speed_hz = ILITEK_SPI_CLK_HZ;
		n++;
	}

	if (n_rx) {
		xfers[n].tx_buf = &tmp_rx;
		xfers[n].rx_buf = rx;
		xfers[n].len    = n_rx;
		xfers[n].speed_hz = ILITEK_SPI_CLK_HZ;
		n++;
	}

	spi_message_init(&msg);
	for (int i = 0; i < n; i++)
		spi_message_add_tail(&xfers[i], &msg);

	ret = spi_sync(ts->spi, &msg);
	if (ret < 0) {
		dev_err(ts->dev, "spi write_then_read error ret=%d\n", ret);
		return ret;
	}
	return 0;
}

/* ---- IC init / reset ---- */

void ilitek_reset(struct ilitek_ts *ts)
{
	/* vendor ilitek_plat_tp_reset: assert low 5ms, high, then edge delay */
	gpiod_set_value(ts->reset_gpio, 1);
	mdelay(1);
	gpiod_set_value(ts->reset_gpio, 0);
	mdelay(5);
	gpiod_set_value(ts->reset_gpio, 1);
	mdelay(5); /* rst_edge_delay = 5 */
}

int ilitek_ic_init(struct ilitek_ts *ts)
{
	u8 cmd[2];
	u8 buf[10] = {0};
	int ret;

	dev_info(ts->dev, "ILITEK TP driver init\n");

	ilitek_reset(ts);

	/* switch to demo mode */
	cmd[0] = P5_X_MODE_CONTROL;
	cmd[1] = P5_X_FW_DEMO_MODE;
	ret = ilitek_spi_write(ts, cmd, 2);
	if (ret < 0) {
		dev_err(ts->dev, "failed to switch demo mode\n");
		return ret;
	}

	/* read TP information (xch/ych) */
	cmd[0] = P5_X_READ_DATA_CTRL;
	cmd[1] = P5_X_GET_TP_INFORMATION;
	ret = ilitek_spi_write_then_read(ts, cmd, 2, buf, 14);
	if (ret < 0) {
		dev_err(ts->dev, "failed to read tp info\n");
		return ret;
	}

	if (buf[0] != P5_X_GET_TP_INFORMATION) {
		dev_err(ts->dev, "invalid tp info response\n");
		return -EINVAL;
	}

	/* vendor hardcodes 18 x 32 for 9881H; panel dims from DTS */
	ts->xch_num = 18;
	ts->ych_num = 32;
	ts->panel_w = ILITEK_PANEL_W;
	ts->panel_h = ILITEK_PANEL_H;

	dev_info(ts->dev, "ILITEK CHIP found: xch=%d ych=%d panel=%ux%u\n",
		 ts->xch_num, ts->ych_num, ts->panel_w, ts->panel_h);
	return 0;
}

/* ---- input device registration ---- */

static int ilitek_input_register(struct ilitek_ts *ts)
{
	struct input_dev *input;

	input = devm_input_allocate_device(ts->dev);
	if (!input) {
		dev_err(ts->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}
	ts->input = input;

	input->name = ILITEK_NAME;
	input->phys = "spi/input0";
	input->dev.parent = ts->dev;
	input->id.bustype = BUS_SPI;

	set_bit(EV_KEY, input->evbit);
	set_bit(BTN_TOUCH, input->keybit);
	set_bit(BTN_TOOL_FINGER, input->keybit);
	set_bit(INPUT_PROP_DIRECT, input->propbit);
	set_bit(EV_ABS, input->evbit);

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, ts->panel_w - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, ts->panel_h - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);

	input_mt_init_slots(input, ILITEK_MAX_TOUCH, INPUT_MT_DIRECT);

	return input_register_device(input);
}

/* ---- demo mode report parsing ---- */

int ilitek_report_demo(struct ilitek_ts *ts, const u8 *buf, int len)
{
	static int curt[ILITEK_MAX_TOUCH];
	static int prev[ILITEK_MAX_TOUCH];
	int finger = 0;
	int i;

	if (len < P5_X_DEMO_PACKET_LENGTH)
		return -EINVAL;
	if (buf[0] != P5_X_DEMO_PACKET_ID)
		return -EINVAL;

	memset(curt, 0, sizeof(curt));

	for (i = 0; i < ILITEK_MAX_TOUCH; i++) {
		u16 xop, yop;

		if (buf[(4 * i) + 1] == 0xFF &&
		    buf[(4 * i) + 2] == 0xFF &&
		    buf[(4 * i) + 3] == 0xFF) {
			curt[i] = 0;
			continue;
		}

		/* vendor packet: 12-bit X split across two bytes, 12-bit Y likewise */
		xop = (((buf[(4 * i) + 1] & 0xF0) << 4) | (buf[(4 * i) + 2]));
		yop = (((buf[(4 * i) + 1] & 0x0F) << 8) | (buf[(4 * i) + 3]));

		input_mt_slot(ts->input, i);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, true);
		input_report_abs(ts->input, ABS_MT_POSITION_X,
				 xop * ts->panel_w / ILITEK_TPD_W);
		input_report_abs(ts->input, ABS_MT_POSITION_Y,
				 yop * ts->panel_h / ILITEK_TPD_H);
		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, 1);
		input_report_abs(ts->input, ABS_MT_WIDTH_MAJOR, 1);

		curt[i] = 1;
		finger++;
	}

	for (i = 0; i < ILITEK_MAX_TOUCH; i++) {
		if (curt[i] == 0 && prev[i] == 1)
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
		prev[i] = curt[i];
	}

	if (finger)
		input_report_key(ts->input, BTN_TOUCH, 1);
	else
		input_report_key(ts->input, BTN_TOUCH, 0);

	input_sync(ts->input);
	return finger;
}

/* ---- IRQ ---- */

static irqreturn_t ilitek_irq_thread(int irq, void *dev_id)
{
	struct ilitek_ts *ts = dev_id;
	u8 buf[P5_X_DEMO_PACKET_LENGTH];
	int ret;

	mutex_lock(&ts->lock);

	if (!ts->report)
		goto out;

	ret = ilitek_spi_read(ts, buf, sizeof(buf));
	if (ret < 0) {
		dev_err(ts->dev, "spi read on irq failed ret=%d\n", ret);
		goto out;
	}

	ilitek_report_demo(ts, buf, ret);
out:
	mutex_unlock(&ts->lock);
	return IRQ_HANDLED;
}

/* ---- probe/remove ---- */

static int ilitek_probe(struct spi_device *spi)
{
	struct ilitek_ts *ts;
	struct device_node *np = spi->dev.of_node;
	int ret;
	u32 speed;

	if (!np) {
		dev_err(&spi->dev, "no DT node\n");
		return -EINVAL;
	}

	ts = devm_kzalloc(&spi->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	spi_set_drvdata(spi, ts);
	ts->spi = spi;
	ts->dev = &spi->dev;
	mutex_init(&ts->lock);
	ts->report = true;

	/* regulators — optional, like the vendor driver (regulators may be
	 * controlled by the panel driver instead). Probe continues without them. */
	ts->vdd = devm_regulator_get(&spi->dev, "vdd");
	if (IS_ERR(ts->vdd)) {
		ret = PTR_ERR(ts->vdd);
		if (ret == -EPROBE_DEFER)
			return ret;
		ts->vdd = NULL;
		dev_warn(&spi->dev, "no vdd regulator: %d\n", ret);
	} else {
		ret = regulator_set_voltage(ts->vdd, ILITEK_VDD_UV, ILITEK_VDD_UV);
		if (ret < 0)
			dev_warn(&spi->dev, "set vdd %d\n", ret);
		ret = regulator_enable(ts->vdd);
		if (ret < 0)
			dev_warn(&spi->dev, "enable vdd %d\n", ret);
	}

	ts->vcc = devm_regulator_get(&spi->dev, "vcc");
	if (IS_ERR(ts->vcc)) {
		ret = PTR_ERR(ts->vcc);
		if (ret == -EPROBE_DEFER)
			return ret;
		ts->vcc = NULL;
		dev_warn(&spi->dev, "no vcc regulator: %d\n", ret);
	} else {
		ret = regulator_set_voltage(ts->vcc, ILITEK_VCC_UV, ILITEK_VCC_UV);
		if (ret < 0)
			dev_warn(&spi->dev, "set vcc %d\n", ret);
		ret = regulator_enable(ts->vcc);
		if (ret < 0)
			dev_warn(&spi->dev, "enable vcc %d\n", ret);
	}

	/* GPIOs from DT: touch,reset-gpio / touch,irq-gpio */
	ts->reset_gpio = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ts->reset_gpio)) {
		ret = PTR_ERR(ts->reset_gpio);
		dev_err(&spi->dev, "get reset-gpio failed %d\n", ret);
		return ret;
	}
	ts->irq_gpio = devm_gpiod_get(&spi->dev, "irq", GPIOD_IN);
	if (IS_ERR(ts->irq_gpio)) {
		ret = PTR_ERR(ts->irq_gpio);
		dev_err(&spi->dev, "get irq-gpio failed %d\n", ret);
		return ret;
	}

	/* SPI setup: mode 0, 8 bits, vendor also set cs-high & cpol/cpha */
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	if (of_property_read_u32(np, "spi-max-frequency", &speed))
		speed = ILITEK_SPI_CLK_HZ;
	spi->max_speed_hz = speed;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup failed %d\n", ret);
		return ret;
	}

	/* IC init + input device */
	ret = ilitek_ic_init(ts);
	if (ret < 0) {
		dev_err(&spi->dev, "ic init failed %d\n", ret);
		return ret;
	}

	ret = ilitek_input_register(ts);
	if (ret < 0) {
		dev_err(&spi->dev, "input register failed %d\n", ret);
		return ret;
	}

	/* threaded IRQ on falling edge per vendor (IRQF_TRIGGER_FALLING) */
	ts->irq = gpiod_to_irq(ts->irq_gpio);
	if (ts->irq < 0) {
		ret = ts->irq;
		dev_err(&spi->dev, "gpiod_to_irq failed %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(&spi->dev, ts->irq, NULL,
					ilitek_irq_thread,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					ILITEK_NAME, ts);
	if (ret < 0) {
		dev_err(&spi->dev, "request irq failed %d\n", ret);
		return ret;
	}

	dev_info(&spi->dev, "ILITEK ITK9881H touch probe ok (irq=%d)\n", ts->irq);
	return 0;
}

static void ilitek_shutdown(struct spi_device *spi)
{
	struct ilitek_ts *ts = spi_get_drvdata(spi);
	if (!ts)
		return;
	ts->report = false;
	if (ts->vdd)
		regulator_disable(ts->vdd);
	if (ts->vcc)
		regulator_disable(ts->vcc);
}

static const struct of_device_id ilitek_of_match[] = {
	{ .compatible = "tchip,ilitek" },
	{ },
};
MODULE_DEVICE_TABLE(of, ilitek_of_match);

static struct spi_driver ilitek_driver = {
	.driver = {
		.name   = ILITEK_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = ilitek_of_match,
	},
	.probe   = ilitek_probe,
	.shutdown = ilitek_shutdown,
};
module_spi_driver(ilitek_driver);

MODULE_AUTHOR("port from Xiaomi ITK9881H driver");
MODULE_DESCRIPTION("Ilitek ITK9881H SPI touch driver - Xiaomi Redmi 8 (olive)");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(ILITEK_DRIVER_VERSION);
