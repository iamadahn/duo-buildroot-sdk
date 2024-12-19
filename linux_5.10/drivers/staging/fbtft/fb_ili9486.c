// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the ILI9341 LCD display controller
 *
 * This display uses 9-bit SPI: Data/Command bit + 8 data bits
 * For platforms that doesn't support 9-bit, the driver is capable
 * of emulating this using 8-bit transfer.
 * This is done by transferring eight 9-bit words in 9 bytes.
 *
 * Copyright (C) 2013 Christian Vogelgsang
 * Based on adafruit22fb.c by Noralf Tronnes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME		"fb_ili9486"
#define WIDTH		320
#define HEIGHT		480
#define TXBUFLEN	(4 * PAGE_SIZE)
#define DEFAULT_GAMMA	"1F 1A 18 0A 0F 06 45 87 32 0A 07 02 07 05 00\n" \
			"00 25 27 05 10 09 3A 78 4D 05 18 0D 38 3A 1F"

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	if (par->gpio.cs)
		gpiod_set_value(par->gpio.cs, 0);  /* Activate chip */

	/* startup sequence for MI0283QT-9A */
	write_reg(par, MIPI_DCS_SOFT_RESET);
	mdelay(5);
	write_reg(par, MIPI_DCS_SET_DISPLAY_OFF);
	/* --------------------------------------------------------- */
	write_reg(par, 0x3A, 0x66);
	write_reg(par, 0xC2, 0x44);
	write_reg(par, 0xCF, 0x00, 0x83, 0x30);
	write_reg(par, 0xF1, 0x36, 0x04, 0x00, 0x3C, 0x0F, 0x8F);
	write_reg(par, 0xF2, 0x18, 0xA3, 0x12, 0x02, 0xB2, 0x12, 0xFF, 0x10, 0x00);
	write_reg(par, 0xF8, 0x21, 0x04);
	write_reg(par, 0xF9, 0x00, 0x08);
	write_reg(par, 0x36, 0x08);
	write_reg(par, 0xB4, 0x00);
	write_reg(par, 0xC1, 0x47);
	write_reg(par, 0xC5, 0x00, 0xAF, 0x80, 0x00);
	write_reg(par, 0xE0, 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F);
	write_reg(par, 0xE1, 0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F);
	write_reg(par, 0x36, 0x48);
	write_reg(par, MIPI_DCS_EXIT_SLEEP_MODE);
	mdelay(100);
	write_reg(par, MIPI_DCS_SET_DISPLAY_ON);
	mdelay(20);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS,
		  (xs >> 8) & 0xFF, xs & 0xFF, (xe >> 8) & 0xFF, xe & 0xFF);

	write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS,
		  (ys >> 8) & 0xFF, ys & 0xFF, (ye >> 8) & 0xFF, ye & 0xFF);

	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
}

#define MEM_Y   BIT(7) /* MY row address order */
#define MEM_X   BIT(6) /* MX column address order */
#define MEM_V   BIT(5) /* MV row / column exchange */
#define MEM_L   BIT(4) /* ML vertical refresh order */
#define MEM_H   BIT(2) /* MH horizontal refresh order */
#define MEM_BGR (3) /* RGB-BGR Order */
static int set_var(struct fbtft_par *par)
{
	switch (par->info->var.rotate) {
	case 0:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  MEM_X | (par->bgr << MEM_BGR));
		break;
	case 270:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  MEM_V | MEM_L | (par->bgr << MEM_BGR));
		break;
	case 180:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  MEM_Y | (par->bgr << MEM_BGR));
		break;
	case 90:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  MEM_Y | MEM_X | MEM_V | (par->bgr << MEM_BGR));
		break;
	}

	return 0;
}

static int write_vmem16_18bus8(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16;
	u8 *txbuf = par->txbuf.buf;
	size_t remain;
	size_t to_copy;
	size_t tx_array_size;
	int i;
	int ret = 0;

	fbtft_par_dbg(DEBUG_WRITE_VMEM, par, "%s(offset=%zu, len=%zu)\n",
		__func__, offset, len);

	/* remaining number of pixels to send */
	remain = len / 2;
	vmem16 = (u16 *)(par->info->screen_buffer + offset);

	if (par->gpio.dc != -1)
		gpiod_set_value(par->gpio.dc, 1);

	/* number of pixels that fits in the transmit buffer */
	tx_array_size = par->txbuf.len / 3;

	while (remain) {
		/* number of pixels to copy in one iteration of the loop */
		to_copy = min(tx_array_size, remain);
		dev_dbg(par->info->device, "    to_copy=%zu, remain=%zu\n",
						to_copy, remain - to_copy);

		for (i = 0; i < to_copy; i++) {
			u16 pixel = vmem16[i];
			u16 r = pixel & 0x1f;
			u16 g = (pixel & (0x3f << 5)) >> 5;
			u16 b = (pixel & (0x1f << 11)) >> 11;

			u8 r8 = (r & 0x1F) << 3;
			u8 g8 = (g & 0x3F) << 2;
			u8 b8 = (b & 0x1F) << 3;

			txbuf[i * 3 + 0] = r8;
			txbuf[i * 3 + 1] = g8;
			txbuf[i * 3 + 2] = b8;
		}

		vmem16 = vmem16 + to_copy;
		ret = par->fbtftops.write(par, par->txbuf.buf, to_copy * 3);
		if (ret < 0)
			return ret;
		remain -= to_copy;
	}
	return ret;
}

/*
 * Gamma string format:
 *  Positive: Par1 Par2 [...] Par15
 *  Negative: Par1 Par2 [...] Par15
 */
#define CURVE(num, idx)  curves[(num) * par->gamma.num_values + (idx)]
static int set_gamma(struct fbtft_par *par, u32 *curves)
{
	int i;

	for (i = 0; i < par->gamma.num_curves; i++)
		write_reg(par, 0xE0 + i,
			  CURVE(i, 0), CURVE(i, 1), CURVE(i, 2),
			  CURVE(i, 3), CURVE(i, 4), CURVE(i, 5),
			  CURVE(i, 6), CURVE(i, 7), CURVE(i, 8),
			  CURVE(i, 9), CURVE(i, 10), CURVE(i, 11),
			  CURVE(i, 12), CURVE(i, 13), CURVE(i, 14));

	return 0;
}

#undef CURVE

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.txbuflen = TXBUFLEN,
	.gamma_num = 2,
	.gamma_len = 15,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.set_gamma = set_gamma,
		.write_vmem = write_vmem16_18bus8,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "ilitek,ili9486", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ili9486");
MODULE_ALIAS("platform:ili9486");

MODULE_DESCRIPTION("FB driver for the ILI9486 LCD display controller");
MODULE_AUTHOR("Christian Vogelgsang");
MODULE_LICENSE("GPL");
