/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_FLASH_SPI_NOR_GD25X512ME_H_
#define ZEPHYR_DRIVERS_FLASH_SPI_NOR_GD25X512ME_H_

/*
 * GD25X512ME command set and timing constants used by flash_gd32_ospi.c.
 *
 * Keep this file chip-specific so command and timing changes can be
 * maintained independently from generic SPI NOR definitions.
 */

/* Status register and polling */
#define SPI_NOR_WIP_BIT               0x01
#define SPI_NOR_WEL_BIT               0x02
#define SPI_NOR_WREN_MATCH            0x02
#define SPI_NOR_WREN_MASK             0x02
#define SPI_NOR_MEM_RDY_MATCH         0x00
#define SPI_NOR_MEM_RDY_MASK          0x01
#define SPI_NOR_AUTO_POLLING_INTERVAL 0x10

/* Read/write dummy cycles */
#define SPI_NOR_DUMMY_RD 8U

/* Geometry */
#define SPI_NOR_PAGE_SIZE   0x0100U
#define SPI_NOR_SECTOR_SIZE 0x1000U
#define SPI_NOR_BLOCK_SIZE  0x10000U

/* Standard opcodes */
#define SPI_NOR_CMD_RDSR      0x05
#define SPI_NOR_CMD_WREN      0x06
#define SPI_NOR_CMD_READ_FAST 0x0B
#define SPI_NOR_CMD_DREAD     0x3B
#define SPI_NOR_CMD_QREAD     0x6B
#define SPI_NOR_CMD_4READ     0xEB
#define SPI_NOR_CMD_PP        0x02
#define SPI_NOR_CMD_PP_1_1_2  0xA2
#define SPI_NOR_CMD_PP_1_1_4  0x32
#define SPI_NOR_CMD_PP_1_4_4  0x38
#define SPI_NOR_CMD_PP_1_1_8  0x82
#define SPI_NOR_CMD_SE        0x20
#define SPI_NOR_CMD_BE        0xD8
#define SPI_NOR_CMD_BULKE     0x60
#define SPI_NOR_CMD_4BA       0xB7
#define SPI_NOR_CMD_RESET_EN  0x66
#define SPI_NOR_CMD_RESET_MEM 0x99

/* 4-byte opcode variants */
#define SPI_NOR_CMD_READ_FAST_4B 0x0C
#define SPI_NOR_CMD_DREAD_4B     0x3C
#define SPI_NOR_CMD_QREAD_4B     0x6C
#define SPI_NOR_CMD_4READ_4B     0xEC
#define SPI_NOR_CMD_PP_4B        0x12
#define SPI_NOR_CMD_PP_1_1_4_4B  0x34
#define SPI_NOR_CMD_PP_1_4_4_4B  0x3EU
#define SPI_NOR_CMD_PP_1_1_8_4B  0x84
#define SPI_NOR_CMD_SE_4B        0x21
#define SPI_NOR_CMD_BE_4B        0xDC

/* GD25X512ME dedicated OPI STR opcodes */
#define GD25X512ME_OCTAL_IO_FAST_READ_CMD             0xCB
#define GD25X512ME_4_BYTE_ADDR_OCTAL_IO_FAST_READ_CMD 0xCC
#define GD25X512ME_EXT_OCTAL_PAGE_PROG_CMD            0xC2
#define GD25X512ME_4_BYTE_EXT_OCTAL_PAGE_PROG_CMD     0x8E

/* GD25X512ME volatile configuration operations */
#define GD25X512ME_WRITE_ENABLE_VOLATILE_STATUS_CFG_CMD 0x50
#define GD25X512ME_WRITE_VOLATILE_CFG_REG_CMD           0x81
#define GD25X512ME_READ_VOLATILE_CFG_REG_CMD            0x85

#define GD25X512ME_CFG_REG0_ADDR       0x00000000U
#define GD25X512ME_CFG_REG1_ADDR       0x00000001U
#define GD25X512ME_CFG_OCTAL_STR       0xB7U
#define GD25X512ME_CFG_OCTAL_STR_WO    0x97U
#define GD25X512ME_CFG_16_DUMMY_CYCLES 0x10U

#endif /* ZEPHYR_DRIVERS_FLASH_SPI_NOR_GD25X512ME_H_ */
