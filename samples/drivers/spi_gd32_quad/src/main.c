/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#define FLASH_NODE DT_NODELABEL(nor_flash)

BUILD_ASSERT(DT_SPI_DEV_HAS_CS_GPIOS(FLASH_NODE),
	     "quad wire mode needs a GPIO chip select");

/* GD25Q16 / generic JEDEC NOR opcodes. */
#define CMD_WRITE_ENABLE      0x06
#define CMD_READ_SR1          0x05
#define CMD_READ_SR2          0x35
#define CMD_WRITE_SR          0x01
#define CMD_WRITE_SR2         0x31
#define CMD_READ_DATA         0x03
#define CMD_QUAD_READ         0x6B
#define CMD_PAGE_PROGRAM      0x02
#define CMD_QUAD_PAGE_PROGRAM 0x32
#define CMD_SECTOR_ERASE      0x20
#define CMD_JEDEC_ID          0x9F

#define SR1_WIP  BIT(0)
#define SR1_WEL  BIT(1)
#define SR1_SRP0 BIT(7)
#define SR2_QE   BIT(1)

#define ADDR_A   0x001000U
#define ADDR_B   0x002000U
#define TEST_LEN 256U

/* SPI_HOLD_ON_CS alone keeps CS asserted across the two phases of a quad
 * transfer, so the single wire opcode and the quad data share one flash
 * transaction. SPI_LOCK_ON must NOT be added: spi_context_lock() identifies the
 * lock owner by spi_config pointer, so switching to the other config would wait
 * forever on a lock this same thread already holds.
 */
#define OP_COMMON (SPI_WORD_SET(8) | SPI_OP_MODE_MASTER | SPI_HOLD_ON_CS)

static const struct device *const spi_dev = DEVICE_DT_GET(DT_BUS(FLASH_NODE));

/* Two distinct objects on purpose: drivers compare spi_config by pointer. */
static const struct spi_config cfg_single = SPI_CONFIG_DT(FLASH_NODE, OP_COMMON);
static const struct spi_config cfg_quad =
	SPI_CONFIG_DT(FLASH_NODE, OP_COMMON | SPI_LINES_QUAD);

static uint8_t pattern_a[TEST_LEN];
static uint8_t pattern_b[TEST_LEN];
static uint8_t buf[TEST_LEN];

static int step_no;

static void step(const char *mode, const char *what)
{
	printk("[%d] %-6s %-24s ", ++step_no, mode, what);
}

static int step_done(int ret)
{
	if (ret < 0) {
		printk("FAIL (%d)%s\n", ret,
		       (ret == -ENOTSUP) ? " driver rejected SPI_LINES_QUAD" : "");
	} else {
		printk("ok\n");
	}

	return ret;
}

static int xfer_single(const uint8_t *tx, uint8_t *rx, size_t len)
{
	const struct spi_buf tb = { .buf = (void *)tx, .len = len };
	const struct spi_buf rb = { .buf = rx, .len = len };
	const struct spi_buf_set txs = { .buffers = &tb, .count = 1 };
	const struct spi_buf_set rxs = { .buffers = &rb, .count = 1 };
	int ret = spi_transceive(spi_dev, &cfg_single, &txs, rx ? &rxs : NULL);

	spi_release(spi_dev, &cfg_single);

	return ret;
}

static int read_sr(uint8_t opcode, uint8_t *val)
{
	uint8_t tx[2] = { opcode, 0U };
	uint8_t rx[2] = { 0U, 0U };
	int ret = xfer_single(tx, rx, sizeof(tx));

	*val = rx[1];

	return ret;
}

static int write_enable(void)
{
	uint8_t tx = CMD_WRITE_ENABLE;

	return xfer_single(&tx, NULL, 1U);
}

static int wait_ready(void)
{
	for (int i = 0; i < 2000; i++) {
		uint8_t sr1;
		int ret = read_sr(CMD_READ_SR1, &sr1);

		if (ret < 0) {
			return ret;
		}
		if ((sr1 & SR1_WIP) == 0U) {
			return 0;
		}
		k_msleep(1);
	}

	return -ETIMEDOUT;
}

/* GD25Q16 only has 0x01, which writes both status bytes at once. 0x31 is the
 * Winbond style single byte command that some pin compatible parts use instead.
 */
static int try_set_qe(uint8_t opcode, uint8_t sr1, uint8_t sr2)
{
	uint8_t tx[3] = { opcode, 0U, 0U };
	size_t len = (opcode == CMD_WRITE_SR) ? 3U : 2U;
	uint8_t status;
	int ret = write_enable();

	if (ret < 0) {
		return ret;
	}

	ret = read_sr(CMD_READ_SR1, &status);
	if (ret < 0) {
		return ret;
	}
	if ((status & SR1_WEL) == 0U) {
		printk("[WEL stays 0 after 0x06] ");
		return -EIO;
	}

	if (opcode == CMD_WRITE_SR) {
		tx[1] = sr1;
		tx[2] = sr2 | SR2_QE;
	} else {
		tx[1] = sr2 | SR2_QE;
	}

	ret = xfer_single(tx, NULL, len);
	if (ret < 0) {
		return ret;
	}

	return wait_ready();
}

static int quad_enable(void)
{
	uint8_t sr1 = 0U;
	uint8_t sr2 = 0U;
	int ret = read_sr(CMD_READ_SR1, &sr1);

	if (ret == 0) {
		ret = read_sr(CMD_READ_SR2, &sr2);
	}
	if (ret < 0) {
		return ret;
	}

	printk("SR1=0x%02x SR2=0x%02x ", sr1, sr2);

	if (sr2 & SR2_QE) {
		return 0;
	}

	ret = try_set_qe(CMD_WRITE_SR, sr1, sr2);
	if (ret == 0) {
		ret = read_sr(CMD_READ_SR2, &sr2);
	}

	if (ret == 0 && (sr2 & SR2_QE) == 0U) {
		printk("[0x01 had no effect, trying 0x31] ");
		ret = try_set_qe(CMD_WRITE_SR2, sr1, sr2);
		if (ret == 0) {
			ret = read_sr(CMD_READ_SR2, &sr2);
		}
	}

	if (ret < 0) {
		return ret;
	}

	printk("-> SR2=0x%02x ", sr2);

	if ((sr2 & SR2_QE) == 0U && (sr1 & SR1_SRP0)) {
		printk("[SRP0 is set, the WP# pin may be locking the register] ");
	}

	return (sr2 & SR2_QE) ? 0 : -EIO;
}

static int sector_erase(uint32_t addr)
{
	uint8_t tx[4] = { CMD_SECTOR_ERASE, addr >> 16, addr >> 8, addr };
	int ret = write_enable();

	if (ret < 0) {
		return ret;
	}

	ret = xfer_single(tx, NULL, sizeof(tx));
	if (ret < 0) {
		return ret;
	}

	return wait_ready();
}

static int program_single(uint32_t addr, const uint8_t *data, size_t len)
{
	uint8_t cmd[4] = { CMD_PAGE_PROGRAM, addr >> 16, addr >> 8, addr };
	const struct spi_buf tb[2] = {
		{ .buf = cmd, .len = sizeof(cmd) },
		{ .buf = (void *)data, .len = len },
	};
	const struct spi_buf_set txs = { .buffers = tb, .count = 2 };
	int ret = write_enable();

	if (ret < 0) {
		return ret;
	}

	ret = spi_transceive(spi_dev, &cfg_single, &txs, NULL);
	spi_release(spi_dev, &cfg_single);
	if (ret < 0) {
		return ret;
	}

	return wait_ready();
}

static int program_quad(uint32_t addr, const uint8_t *data, size_t len)
{
	uint8_t cmd[4] = { CMD_QUAD_PAGE_PROGRAM, addr >> 16, addr >> 8, addr };
	const struct spi_buf cb = { .buf = cmd, .len = sizeof(cmd) };
	const struct spi_buf_set cmds = { .buffers = &cb, .count = 1 };
	const struct spi_buf db = { .buf = (void *)data, .len = len };
	const struct spi_buf_set datas = { .buffers = &db, .count = 1 };
	int ret = write_enable();

	if (ret < 0) {
		return ret;
	}

	ret = spi_transceive(spi_dev, &cfg_single, &cmds, NULL);
	if (ret == 0) {
		/* Marks the single wire phase, so a hang below is clearly the quad one. */
		printk("[cmd sent] ");
		ret = spi_transceive(spi_dev, &cfg_quad, &datas, NULL);
	}
	spi_release(spi_dev, &cfg_quad);
	if (ret < 0) {
		return ret;
	}

	return wait_ready();
}

static int read_single(uint32_t addr, uint8_t *buf, size_t len)
{
	uint8_t cmd[4] = { CMD_READ_DATA, addr >> 16, addr >> 8, addr };
	const struct spi_buf tb[2] = {
		{ .buf = cmd, .len = sizeof(cmd) },
		{ .buf = NULL, .len = len },
	};
	const struct spi_buf rb[2] = {
		{ .buf = NULL, .len = sizeof(cmd) },
		{ .buf = buf, .len = len },
	};
	const struct spi_buf_set txs = { .buffers = tb, .count = 2 };
	const struct spi_buf_set rxs = { .buffers = rb, .count = 2 };
	int ret = spi_transceive(spi_dev, &cfg_single, &txs, &rxs);

	spi_release(spi_dev, &cfg_single);

	return ret;
}

static int read_quad(uint32_t addr, uint8_t *buf, size_t len)
{
	uint8_t cmd[4] = { CMD_QUAD_READ, addr >> 16, addr >> 8, addr };
	/* 0x6B needs 8 dummy clocks, which is four frames once the bus is quad. */
	uint8_t dummy[4];
	const struct spi_buf cb = { .buf = cmd, .len = sizeof(cmd) };
	const struct spi_buf_set cmds = { .buffers = &cb, .count = 1 };
	const struct spi_buf db[2] = {
		{ .buf = dummy, .len = sizeof(dummy) },
		{ .buf = buf, .len = len },
	};
	const struct spi_buf_set datas = { .buffers = db, .count = 2 };
	int ret = spi_transceive(spi_dev, &cfg_single, &cmds, NULL);

	if (ret == 0) {
		/* Marks the single wire phase, so a hang below is clearly the quad one. */
		printk("[cmd sent] ");
		ret = spi_transceive(spi_dev, &cfg_quad, NULL, &datas);
	}

	spi_release(spi_dev, &cfg_quad);

	return ret;
}

static void dump(const char *tag, const uint8_t *buf, size_t len)
{
	printk("    %s:", tag);
	for (size_t i = 0; i < len; i++) {
		printk("%s%02x", (i % 16U) ? " " : "\n      ", buf[i]);
	}
	printk("\n");
}

/* A dead quad IO line shows up as one bit per nibble always wrong, so counting
 * errors per bit position points straight at the offending SPI_IOn pin.
 */
static void diagnose(const uint8_t *ref, const uint8_t *got, size_t len)
{
	uint32_t per_bit[8] = { 0 };

	for (size_t i = 0; i < len; i++) {
		uint8_t diff = ref[i] ^ got[i];

		for (int b = 0; b < 8; b++) {
			if (diff & BIT(b)) {
				per_bit[b]++;
			}
		}
	}

	printk("    bit error counts out of %u bytes:", (unsigned int)len);
	for (int b = 0; b < 8; b++) {
		printk(" b%d=%u", b, per_bit[b]);
	}
	printk("\n");

	for (int b = 0; b < 4; b++) {
		if (per_bit[b] == len && per_bit[b + 4] == len) {
			printk("    bits %d and %d always wrong -> SPI_IO%d is not "
			       "connected, check the pin and its jumper\n", b, b + 4, b);
		}
	}
}

static bool compare(const uint8_t *want, const uint8_t *got, size_t len,
		    const char *subject)
{
	dump("write", want, len);
	dump("read ", got, len);

	if (memcmp(want, got, len) == 0) {
		printk("    => MATCH, quad %s works\n", subject);
		return true;
	}

	printk("    => MISMATCH, quad %s is broken\n", subject);
	diagnose(want, got, len);

	return false;
}

/* The driver picks its transfer path at build time, so state it here instead of
 * leaving it to be inferred from the build command.
 */
static void print_setup(void)
{
	const char *path;

	if (IS_ENABLED(CONFIG_SPI_GD32_DMA) &&
	    DT_NODE_HAS_PROP(DT_BUS(FLASH_NODE), dmas)) {
		path = "DMA";
	} else if (IS_ENABLED(CONFIG_SPI_GD32_INTERRUPT)) {
		path = "interrupt";
	} else {
		path = "polling";
	}

	printk("data path: %s\n", path);
	printk("quad wire: %s\n\n",
	       (IS_ENABLED(CONFIG_SPI_EXTENDED_MODES) &&
		DT_PROP(DT_BUS(FLASH_NODE), quad_capable))
		       ? "enabled" : "NOT available");
}

int main(void)
{
	uint8_t id[4] = { 0 };
	uint8_t tx_id[4] = { CMD_JEDEC_ID, 0, 0, 0 };
	bool quad_write_ok = false;
	bool quad_read_ok = false;
	int ret;

	for (size_t i = 0; i < TEST_LEN; i++) {
		pattern_a[i] = (uint8_t)(i * 7U + 0x5AU);
		pattern_b[i] = (uint8_t)(i * 13U + 0xA3U);
	}

	printk("\nGD32 SPI quad wire mode test\n");
	printk("============================\n");
	printk("SINGLE = SPI_LINES_SINGLE, QUAD = SPI_LINES_QUAD\n");

	if (!device_is_ready(spi_dev)) {
		printk("%s not ready\n", spi_dev->name);
		return 0;
	}
	printk("bus %s, %u Hz\n", spi_dev->name, cfg_single.frequency);
	print_setup();

	step("SINGLE", "read JEDEC ID 0x9F");
	ret = xfer_single(tx_id, id, sizeof(tx_id));
	printk("%02x %02x %02x ", id[1], id[2], id[3]);
	if (step_done(ret) < 0) {
		return 0;
	}
	if (id[1] == 0x00U || id[1] == 0xFFU) {
		printk("bus looks dead, stopping\n");
		return 0;
	}

	step("SINGLE", "set QE in SR2");
	if (step_done(quad_enable()) < 0) {
		printk("flash will not accept quad commands without QE, stopping\n");
		return 0;
	}

	printk("\n--- Test A: QUAD write, SINGLE read back ---\n");

	step("SINGLE", "sector erase 0x20");
	if (step_done(sector_erase(ADDR_A)) < 0) {
		return 0;
	}

	step("QUAD", "page program 0x32");
	ret = step_done(program_quad(ADDR_A, pattern_a, TEST_LEN));
	if (ret == 0) {
		step("SINGLE", "read back 0x03");
		ret = step_done(read_single(ADDR_A, buf, TEST_LEN));
	}
	if (ret == 0) {
		quad_write_ok = compare(pattern_a, buf, TEST_LEN, "WRITE");
	}

	printk("\n--- Test B: SINGLE write, QUAD read back ---\n");

	step("SINGLE", "sector erase 0x20");
	if (step_done(sector_erase(ADDR_B)) < 0) {
		return 0;
	}

	step("SINGLE", "page program 0x02");
	ret = step_done(program_single(ADDR_B, pattern_b, TEST_LEN));
	if (ret == 0) {
		memset(buf, 0xA5, TEST_LEN);
		step("QUAD", "read 0x6B");
		ret = step_done(read_quad(ADDR_B, buf, TEST_LEN));
	}
	if (ret == 0) {
		quad_read_ok = compare(pattern_b, buf, TEST_LEN, "READ");

		if (!quad_read_ok && buf[0] == 0xA5U) {
			printk("    buffer untouched: no clock in read direction, the RX "
			       "branch of spi_gd32_frame_exchange needs a dummy TX write\n");
		}
	}

	printk("\n============================\n");
	printk("quad WRITE 0x32: %s\n", quad_write_ok ? "PASS" : "FAIL");
	printk("quad READ  0x6B: %s\n", quad_read_ok ? "PASS" : "FAIL");

	return 0;
}
