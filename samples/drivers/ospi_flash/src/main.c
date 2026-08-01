/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <stdio.h>
#include <string.h>

#if defined(CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED)
#include <zephyr/drivers/flash/gd32_ospi_flash_api_extensions.h>
#endif

#define SPI_FLASH_TEST_DATA_LEN CONFIG_SPI_FLASH_TEST_DATA_LEN

#define SPI_FLASH_TEST_REGION_OFFSET 0xff000U
#define SPI_FLASH_SECTOR_SIZE        4096U

#define SPI_FLASH_MULTI_SECTOR_TEST

#define SPI_FLASH_COMPAT gd_gd32_ospi_nor

static void fill_test_pattern(uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = (uint8_t)(0x5A ^ (i * 37U));
	}
}

static bool is_erased_pattern(const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (buf[i] != 0xFFU) {
			return false;
		}
	}

	return true;
}

void single_sector_test(const struct device *flash_dev)
{
	uint8_t expected[SPI_FLASH_TEST_DATA_LEN];
	const size_t len = sizeof(expected);
	uint8_t buf[sizeof(expected)];
	int rc;

	fill_test_pattern(expected, len);

	printf("\nPerform test on single sector");
	/* Write protection needs to be disabled before each write or
	 * erase, since the flash component turns on write protection
	 * automatically after completion of write and erase
	 * operations.
	 */
	printf("\nTest 1: Flash erase\n");

	/* Full flash erase if SPI_FLASH_TEST_REGION_OFFSET = 0 and
	 * SPI_FLASH_SECTOR_SIZE = flash size
	 */
	rc = flash_erase(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, SPI_FLASH_SECTOR_SIZE);
	if (rc != 0) {
		printf("Flash erase failed! %d\n", rc);
		return;
	}

	/* Check erased pattern */
	memset(buf, 0, len);
	rc = flash_read(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, buf, len);
	if (rc != 0) {
		printf("Flash read failed! %d\n", rc);
		return;
	}
	if (!is_erased_pattern(buf, len)) {
		printf("Flash erase failed at offset 0x%x got %02x %02x %02x %02x\n",
			SPI_FLASH_TEST_REGION_OFFSET, buf[0], buf[1], buf[2], buf[3]);
		return;
	}
	printf("Flash erase succeeded!\n");

	printf("\nTest 2: Flash write\n");

	printf("Attempting to write %zu bytes\n", len);
	rc = flash_write(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, expected, len);
	if (rc != 0) {
		printf("Flash write failed! %d\n", rc);
		return;
	}

	memset(buf, 0, len);
	rc = flash_read(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, buf, len);
	if (rc != 0) {
		printf("Flash read failed! %d\n", rc);
		return;
	}

	if (memcmp(expected, buf, len) == 0) {
		printf("Data read matches data written. Good!!\n");
	} else {
		const uint8_t *wp = expected;
		const uint8_t *rp = buf;
		const uint8_t *rpe = rp + len;

		printf("Data read does not match data written!!\n");
		while (rp < rpe) {
			printf("%08x wrote %02x read %02x %s\n",
			       (uint32_t)(SPI_FLASH_TEST_REGION_OFFSET + (rp - buf)), *wp, *rp,
			       (*rp == *wp) ? "match" : "MISMATCH");
			++rp;
			++wp;
		}
	}
}

#if defined SPI_FLASH_MULTI_SECTOR_TEST
void multi_sector_test(const struct device *flash_dev)
{
	uint8_t expected[SPI_FLASH_TEST_DATA_LEN];
	const size_t len = sizeof(expected);
	uint8_t buf[sizeof(expected)];
	int rc;

	fill_test_pattern(expected, len);

	printf("\nPerform test on multiple consecutive sectors");

	/* Write protection needs to be disabled before each write or
	 * erase, since the flash component turns on write protection
	 * automatically after completion of write and erase
	 * operations.
	 */
	printf("\nTest 1: Flash erase\n");

	/* Full flash erase if SPI_FLASH_TEST_REGION_OFFSET = 0 and
	 * SPI_FLASH_SECTOR_SIZE = flash size
	 * Erase 2 sectors for check for erase of consequtive sectors
	 */
	rc = flash_erase(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, SPI_FLASH_SECTOR_SIZE * 2);
	if (rc != 0) {
		printf("Flash erase failed! %d\n", rc);
		return;
	}

	/* Read the content and check for erased */
	memset(buf, 0, len);
	size_t offs = SPI_FLASH_TEST_REGION_OFFSET;

	while (offs < SPI_FLASH_TEST_REGION_OFFSET + 2 * SPI_FLASH_SECTOR_SIZE) {
		rc = flash_read(flash_dev, offs, buf, len);
		if (rc != 0) {
			printf("Flash read failed! %d\n", rc);
			return;
		}
		if (!is_erased_pattern(buf, len)) {
			printf("Flash erase failed at offset 0x%x got %02x %02x %02x "
				"%02x\n",
				offs, buf[0], buf[1], buf[2], buf[3]);
			return;
		}
		offs += SPI_FLASH_SECTOR_SIZE;
	}
	printf("Flash erase succeeded!\n");

	printf("\nTest 2: Flash write\n");

	offs = SPI_FLASH_TEST_REGION_OFFSET;

	while (offs < SPI_FLASH_TEST_REGION_OFFSET + 2 * SPI_FLASH_SECTOR_SIZE) {
		printf("Attempting to write %zu bytes at offset 0x%x\n", len, offs);
		rc = flash_write(flash_dev, offs, expected, len);
		if (rc != 0) {
			printf("Flash write failed! %d\n", rc);
			return;
		}

		memset(buf, 0, len);
		rc = flash_read(flash_dev, offs, buf, len);
		if (rc != 0) {
			printf("Flash read failed! %d\n", rc);
			return;
		}

		if (memcmp(expected, buf, len) == 0) {
			printf("Data read matches data written. Good!!\n");
		} else {
			const uint8_t *wp = expected;
			const uint8_t *rp = buf;
			const uint8_t *rpe = rp + len;

			printf("Data read does not match data written!!\n");
			while (rp < rpe) {
				printf("%08x wrote %02x read %02x %s\n",
				       (uint32_t)(offs + (rp - buf)), *wp, *rp,
				       (*rp == *wp) ? "match" : "MISMATCH");
				++rp;
				++wp;
			}
		}
		offs += SPI_FLASH_SECTOR_SIZE;
	}
}
#endif

#if defined(CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED)
/*
 * OSPI controller's memory-mapped address window (the second "reg" entry of
 * the OSPI controller node, e.g. 0x90000000 for ospi0 on gd32h759i_eval).
 */
#define SPI_FLASH_MMAP_BASE                                                                        \
	DT_REG_ADDR_BY_IDX(DT_PARENT(DT_COMPAT_GET_ANY_STATUS_OKAY(SPI_FLASH_COMPAT)), 1)

void memory_mapped_test(const struct device *flash_dev)
{
	uint8_t expected[SPI_FLASH_TEST_DATA_LEN];
	const size_t len = sizeof(expected);
	uint8_t buf[sizeof(expected)];

	const volatile uint8_t *mmap_ptr =
		(const volatile uint8_t *)(SPI_FLASH_MMAP_BASE + SPI_FLASH_TEST_REGION_OFFSET);

	int rc;

	fill_test_pattern(expected, len);

	printf("\nPerform memory-mapped read test");

	printf("\nTest 1: Flash erase\n");
	rc = flash_erase(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, SPI_FLASH_SECTOR_SIZE);
	if (rc != 0) {
		printf("Flash erase failed! %d\n", rc);
		return;
	}
	printf("Flash erase succeeded!\n");

	printf("\nTest 2: Indirect (8-line octal) write, memory-mapped read\n");
	rc = flash_write(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, expected, len);
	if (rc != 0) {
		printf("Flash write failed! %d\n", rc);
		return;
	}

	/*
	 * flash_read() only uses the memory-mapped fast path if memory-mapped
	 * mode has already been explicitly enabled; every other read in this
	 * sample (erase/write verification) stays on the indirect-mode path.
	 * Enable it here to exercise the memory-mapped path for this read.
	 */
	rc = flash_gd32_ospi_memory_mapped_enable(flash_dev);
	if (rc != 0) {
		printf("Memory-mapped mode enable failed! %d\n", rc);
		return;
	}

	memset(buf, 0, len);
	rc = flash_read(flash_dev, SPI_FLASH_TEST_REGION_OFFSET, buf, len);
	if (rc != 0) {
		printf("Flash read failed! %d\n", rc);
		return;
	}

	if (memcmp(expected, buf, len) != 0) {
		printf("Memory-mapped flash_read() data mismatch!!\n");
		return;
	}
	printf("flash_read() via memory-mapped mode succeeded!\n");

	/*
	 * flash_read() leaves the controller in memory-mapped mode on
	 * success, so the OSPI address window can now be dereferenced
	 * directly by the CPU, matching the vendor demo's raw pointer access.
	 */
	memset(buf, 0, len);
	for (size_t i = 0; i < len; i++) {
		buf[i] = mmap_ptr[i];
	}

	if (memcmp(expected, buf, len) == 0) {
		printf("Direct pointer read at 0x%08lx matches data written. Good!!\n",
		       (unsigned long)mmap_ptr);
	} else {
		printf("Direct pointer read at 0x%08lx does not match data written!!\n",
		       (unsigned long)mmap_ptr);
	}
}
#endif /* CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED */

int main(void)
{
	const struct device *flash_dev = DEVICE_DT_GET_ONE(SPI_FLASH_COMPAT);

	if (!device_is_ready(flash_dev)) {
		printk("%s: device not ready.\n", flash_dev->name);
		return 0;
	}

	printf("\n%s GD32 OSPI flash testing\n", flash_dev->name);
	printf("==========================\n");

	single_sector_test(flash_dev);
#if defined SPI_FLASH_MULTI_SECTOR_TEST
	multi_sector_test(flash_dev);
#endif
#if defined(CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED)
	memory_mapped_test(flash_dev);
#endif
	return 0;
}
