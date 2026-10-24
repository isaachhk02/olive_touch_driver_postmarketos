/* SPDX-License-Identifier: GPL-2.0 */
/* Ilitek ITK9881H (ILI9881H) touch driver core — Xiaomi Redmi 8 (olive)
 * Ported to mainline Linux (PostMarketOS 26.06) from Xiaomi msm-4.9 driver.
 */

#ifndef _ILITEK_CORE_H_
#define _ILITEK_CORE_H_

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

/* ---- driver / chip constants (from vendor ilitek.h) ---- */
#define ILITEK_DRIVER_VERSION	"2.3.1.0-pmos"
#define ILITEK_VENDOR_INFO	"[Vendor]Tianma(TP) + Tianma(LCD), [TP-IC]ILITEK9881H"

#define ILITEK_VDD_UV		1800000
#define ILITEK_VCC_UV		1800000
#define ILITEK_SPI_CLK_HZ	10000000

#define ILITEK_PANEL_W		720
#define ILITEK_PANEL_H		1520
#define ILITEK_TPD_W		2048
#define ILITEK_TPD_H		2048
#define ILITEK_MAX_TOUCH	10

/* ---- SPI protocol commands (vendor ilitek.h) ---- */
#define SPI_WRITE			0x82
#define SPI_READ			0x83
#define SPI_ACK			0xA3

#define P5_X_MODE_CONTROL		0xF0
#define P5_X_FW_DEMO_MODE		0x00
#define P5_X_READ_DATA_CTRL		0xF6
#define P5_X_CDC_BUSY_STATE		0xF3
#define P5_X_GET_TP_INFORMATION	0x20
#define P5_X_GET_PANEL_INFORMATION	0x29

#define P5_X_DEMO_PACKET_LENGTH	43
#define P5_X_DEMO_PACKET_ID		0x5A

/* chip ids (vendor ilitek.h) */
#define ILI9881_CHIP			0x9881
#define ILI9881H_AD			0x98811103
#define ILI9881H_AE			0x98811104

/* ---- device state ---- */
struct ilitek_ts {
	struct spi_device		*spi;
	struct input_dev		*input;
	struct device			*dev;
	struct gpio_desc		*reset_gpio;
	struct gpio_desc		*irq_gpio;
	int				irq;
	struct regulator		*vdd;
	struct regulator		*vcc;

	struct mutex			lock;
	struct delayed_work		esd_work;
	bool				suspended;

	u16				panel_w;
	u16				panel_h;
	u8				xch_num;
	u8				ych_num;

	bool				report;
};

/* spi read/write helpers */
int ilitek_spi_write(struct ilitek_ts *ts, const u8 *buf, int len);
int ilitek_spi_read(struct ilitek_ts *ts, u8 *buf, int len);
int ilitek_spi_write_then_read(struct ilitek_ts *ts,
			       const u8 *tx, unsigned n_tx,
			       u8 *rx, unsigned n_rx);

/* ic functions */
int ilitek_ic_init(struct ilitek_ts *ts);
void ilitek_reset(struct ilitek_ts *ts);
int ilitek_report_demo(struct ilitek_ts *ts, const u8 *buf, int len);

#endif /* _ILITEK_CORE_H_ */
