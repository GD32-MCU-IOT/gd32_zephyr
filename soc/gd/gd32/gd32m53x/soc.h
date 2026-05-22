/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_GD_GD32_GD32M531_SOC_H_
#define ZEPHYR_SOC_GD_GD32_GD32M531_SOC_H_

#include <gd32m53x.h>

/*
 * I2C single-instance compatibility shim.
 * GD32M531 HAL functions have no periph parameter; Zephyr I2C V2 driver
 * passes periph as the first argument. These macros transparently discard
 * the periph argument.
 *
 * Safe: HAL .c files do NOT include soc.h (verified via build.ninja).
 */
#ifdef CONFIG_USE_GD32_I2C_V2

/* Register macros: object-like -> function-like (accept periph base address) */
#undef I2C_CTL0
#undef I2C_CTL1
#undef I2C_STAT
#undef I2C_STATC
#undef I2C_TDATA
#undef I2C_RDATA
#undef I2C_TIMING

#define I2C_CTL0(i2cx)   REG32(I2C + 0x00000000U)
#define I2C_CTL1(i2cx)   REG32(I2C + 0x00000004U)
#define I2C_STAT(i2cx)   REG32(I2C + 0x00000018U)
#define I2C_STATC(i2cx)  REG32(I2C + 0x0000001CU)
#define I2C_TDATA(i2cx)  REG32(I2C + 0x00000028U)
#define I2C_RDATA(i2cx)  REG32(I2C + 0x00000024U)
#define I2C_TIMING(i2cx) REG32(I2C + 0x00000010U)

/* void f(periph) -> void f(void) */
#define i2c_enable(periph)                       ((void)(periph), i2c_enable())
#define i2c_disable(periph)                      ((void)(periph), i2c_disable())
#define i2c_start_on_bus(periph)                 ((void)(periph), i2c_start_on_bus())
#define i2c_stop_on_bus(periph)                  ((void)(periph), i2c_stop_on_bus())
#define i2c_reload_enable(periph)                ((void)(periph), i2c_reload_enable())
#define i2c_reload_disable(periph)               ((void)(periph), i2c_reload_disable())
#define i2c_automatic_end_enable(periph)         ((void)(periph), i2c_automatic_end_enable())
#define i2c_automatic_end_disable(periph)        ((void)(periph), i2c_automatic_end_disable())
#define i2c_address10_enable(periph)             ((void)(periph), i2c_address10_enable())
#define i2c_address10_disable(periph)            ((void)(periph), i2c_address10_disable())
#define i2c_address10_header_disable(periph)     ((void)(periph), i2c_address10_header_disable())
#define i2c_address_disable(periph)              ((void)(periph), i2c_address_disable())
#define i2c_stretch_scl_low_enable(periph)       ((void)(periph), i2c_stretch_scl_low_enable())

/* void f(periph, arg...) -> void f(arg...) */
#define i2c_flag_get(periph, flag)               ((void)(periph), i2c_flag_get(flag))
#define i2c_flag_clear(periph, flag)             ((void)(periph), i2c_flag_clear(flag))
#define i2c_interrupt_enable(periph, intr)       ((void)(periph), i2c_interrupt_enable(intr))
#define i2c_interrupt_disable(periph, intr)      ((void)(periph), i2c_interrupt_disable(intr))
#define i2c_dma_enable(periph, dma)              ((void)(periph), i2c_dma_enable(dma))
#define i2c_dma_disable(periph, dma)             ((void)(periph), i2c_dma_disable(dma))
#define i2c_data_transmit(periph, data) \
	((void)(periph), i2c_data_transmit((uint8_t)(data)))
#define i2c_data_receive(periph)                 ((void)(periph), (uint32_t)i2c_data_receive())
#define i2c_master_addressing(periph, addr, dir) ((void)(periph), i2c_master_addressing(addr, dir))
#define i2c_address_config(periph, addr, fmt)    ((void)(periph), i2c_address_config(addr, fmt))
#define i2c_timing_config(periph, psc, sdely, ddely) \
	((void)(periph), i2c_timing_config(psc, sdely, ddely))
#define i2c_master_clock_config(periph, sclh, scll) \
	((void)(periph), i2c_master_clock_config(sclh, scll))
#define i2c_transfer_byte_number_config(periph, n) \
	((void)(periph), i2c_transfer_byte_number_config((uint8_t)(n)))

#endif /* CONFIG_USE_GD32_I2C_V2 */

/*
 * SPI compatibility shim.
 * GD32M53x HAL defines SPI register macros without a base-address argument;
 * Zephyr SPI driver passes the periph base register address. These macros
 * transparently wrap the object-like HAL macros into function-like macros
 * that accept the SPIx base address and apply the proper register offset.
 *
 * Safe: HAL .c files do NOT include soc.h (verified via build.ninja).
 */
#ifdef CONFIG_SPI_GD32

#undef SPI_CTL0
#undef SPI_CTL1
#undef SPI_STAT
#undef SPI_DATA
#undef SPI_CRCPOLY
#undef SPI_RCRC
#undef SPI_TCRC
#undef SPI_QCTL

#define SPI_CTL0(spix)    REG32((spix) + 0x00000000U)
#define SPI_CTL1(spix)    REG32((spix) + 0x00000004U)
#define SPI_STAT(spix)    REG32((spix) + 0x00000008U)
#define SPI_DATA(spix)    REG32((spix) + 0x0000000CU)
#define SPI_CRCPOLY(spix) REG32((spix) + 0x00000010U)
#define SPI_RCRC(spix)    REG32((spix) + 0x00000014U)
#define SPI_TCRC(spix)    REG32((spix) + 0x00000018U)
#define SPI_QCTL(spix)    REG32((spix) + 0x00000080U)

#endif /* CONFIG_SPI_GD32 */

#endif /* ZEPHYR_SOC_GD_GD32_GD32M531_SOC_H_ */
