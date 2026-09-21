/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_GD_GD32_GD32M531_SOC_H_
#define ZEPHYR_SOC_GD_GD32_GD32M531_SOC_H_

#include <gd32m53x.h>

/*
 * USART compatibility shim.
 * GD32M53x has UART (not USART) peripherals; the HAL therefore exposes a
 * uart_xxx API. These macros map the usart_xxx names used by the Zephyr
 * driver onto the uart_xxx HAL API.
 *
 * Safe: HAL .c files do NOT include soc.h (verified via build.ninja).
 */
#ifdef CONFIG_USART_GD32

/* Register/data access */
#define USART_STAT(x)  UART_STAT0(x)
#define USART_STAT0(x) UART_STAT0(x)
#define USART_DATA(x)  UART_DATA(x)

/* Flags */
#define USART_FLAG_RBNE  UART_FLAG_RBNE
#define USART_FLAG_TBE   UART_FLAG_TBE
#define USART_FLAG_TC    UART_FLAG_TC
#define USART_FLAG_ORERR UART_FLAG_ORERR
#define USART_FLAG_NERR  UART_FLAG_NERR
#define USART_FLAG_FERR  UART_FLAG_FERR
#define USART_FLAG_PERR  UART_FLAG_PERR
#define USART_FLAG_IDLE  UART_FLAG_IDLE

/* Interrupts */
#define USART_INT_RBNE      UART_INT_RBNE
#define USART_INT_TBE       UART_INT_TBE
#define USART_INT_TC        UART_INT_TC
#define USART_INT_ERR       UART_INT_ERR
#define USART_INT_IDLE      UART_INT_IDLE
#define USART_INT_PERR      UART_INT_PERR
#define USART_INT_FLAG_RBNE UART_INT_FLAG_RBNE
#define USART_INT_FLAG_TC   UART_INT_FLAG_TC
#define USART_INT_FLAG_IDLE UART_INT_FLAG_IDLE

/* RX/TX enable */
#define USART_RECEIVE_ENABLE  UART_RECEIVE_ENABLE
#define USART_TRANSMIT_ENABLE UART_TRANSMIT_ENABLE

/* DMA enable/disable */
#define USART_RECEIVE_DMA_ENABLE   UART_RECEIVE_DMA_ENABLE
#define USART_RECEIVE_DMA_DISABLE  UART_RECEIVE_DMA_DISABLE
#define USART_TRANSMIT_DMA_ENABLE  UART_TRANSMIT_DMA_ENABLE
#define USART_TRANSMIT_DMA_DISABLE UART_TRANSMIT_DMA_DISABLE

/* Stop bits */
#define USART_STB_1BIT   UART_STB_1BIT
#define USART_STB_2BIT   UART_STB_2BIT
#define USART_STB_0_5BIT CTL1_STB(1)
#define USART_STB_1_5BIT CTL1_STB(3)

/* Parity */
#define USART_PM_NONE UART_PM_NONE
#define USART_PM_ODD  UART_PM_ODD
#define USART_PM_EVEN UART_PM_EVEN

/* Word length */
#define USART_WL_8BIT UART_WL_8BIT
#define USART_WL_9BIT UART_WL_9BIT
#define USART_WL_7BIT UART_WL_7BIT

/* M53x UART has no hardware flow control */
#define USART_RTS_ENABLE  0U
#define USART_RTS_DISABLE 1U
#define USART_CTS_ENABLE  0U
#define USART_CTS_DISABLE 1U

/* Function mappings */
#define usart_baudrate_set         uart_baudrate_set
#define usart_word_length_set      uart_word_length_set
#define usart_stop_bit_set         uart_stop_bit_set
#define usart_parity_config        uart_parity_config
#define usart_enable               uart_enable
#define usart_disable              uart_disable
#define usart_transmit_config      uart_transmit_config
#define usart_receive_config       uart_receive_config
#define usart_interrupt_enable     uart_interrupt_enable
#define usart_interrupt_disable    uart_interrupt_disable
#define usart_interrupt_flag_get   uart_interrupt_flag_get
#define usart_interrupt_flag_clear uart_interrupt_flag_clear
#define usart_flag_get             uart_flag_get
#define usart_flag_clear           uart_flag_clear
#define usart_data_receive         uart_data_receive
#define usart_data_transmit        uart_data_transmit
#define usart_dma_receive_config   uart_dma_receive_config
#define usart_dma_transmit_config  uart_dma_transmit_config

#define usart_hardware_flow_rts_config(periph, rtsconfig)                                          \
	do {                                                                                       \
		(void)(periph);                                                                    \
		(void)(rtsconfig);                                                                 \
	} while (0)
#define usart_hardware_flow_cts_config(periph, ctsconfig)                                          \
	do {                                                                                       \
		(void)(periph);                                                                    \
		(void)(ctsconfig);                                                                 \
	} while (0)

#endif /* CONFIG_USART_GD32 */

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
#define i2c_enable(periph)                   ((void)(periph), i2c_enable())
#define i2c_disable(periph)                  ((void)(periph), i2c_disable())
#define i2c_start_on_bus(periph)             ((void)(periph), i2c_start_on_bus())
#define i2c_stop_on_bus(periph)              ((void)(periph), i2c_stop_on_bus())
#define i2c_reload_enable(periph)            ((void)(periph), i2c_reload_enable())
#define i2c_reload_disable(periph)           ((void)(periph), i2c_reload_disable())
#define i2c_automatic_end_enable(periph)     ((void)(periph), i2c_automatic_end_enable())
#define i2c_automatic_end_disable(periph)    ((void)(periph), i2c_automatic_end_disable())
#define i2c_address10_enable(periph)         ((void)(periph), i2c_address10_enable())
#define i2c_address10_disable(periph)        ((void)(periph), i2c_address10_disable())
#define i2c_address10_header_disable(periph) ((void)(periph), i2c_address10_header_disable())
#define i2c_address_disable(periph)          ((void)(periph), i2c_address_disable())
#define i2c_stretch_scl_low_enable(periph)   ((void)(periph), i2c_stretch_scl_low_enable())

/* void f(periph, arg...) -> void f(arg...) */
#define i2c_flag_get(periph, flag)          ((void)(periph), i2c_flag_get(flag))
#define i2c_flag_clear(periph, flag)        ((void)(periph), i2c_flag_clear(flag))
#define i2c_interrupt_enable(periph, intr)  ((void)(periph), i2c_interrupt_enable(intr))
#define i2c_interrupt_disable(periph, intr) ((void)(periph), i2c_interrupt_disable(intr))
#define i2c_dma_enable(periph, dma)         ((void)(periph), i2c_dma_enable(dma))
#define i2c_dma_disable(periph, dma)        ((void)(periph), i2c_dma_disable(dma))
#define i2c_data_transmit(periph, data)     ((void)(periph), i2c_data_transmit((uint8_t)(data)))
#define i2c_data_receive(periph)            ((void)(periph), (uint32_t)i2c_data_receive())
#define i2c_master_addressing(periph, addr, dir) ((void)(periph), i2c_master_addressing(addr, dir))
#define i2c_address_config(periph, addr, fmt)    ((void)(periph), i2c_address_config(addr, fmt))
#define i2c_timing_config(periph, psc, sdely, ddely)                                               \
	((void)(periph), i2c_timing_config(psc, sdely, ddely))
#define i2c_master_clock_config(periph, sclh, scll)                                                \
	((void)(periph), i2c_master_clock_config(sclh, scll))
#define i2c_transfer_byte_number_config(periph, n)                                                 \
	((void)(periph), i2c_transfer_byte_number_config((uint8_t)(n)))

#endif /* CONFIG_USE_GD32_I2C_V2 */

/*
 * SPI single-instance compatibility shim.
 * GD32M53x HAL defines SPI register macros without a base-address argument;
 * Zephyr SPI driver passes the periph base register address. These macros
 * turn the object-like HAL macros into function-like ones that discard the
 * periph argument.
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

#define SPI_CTL0(spix)    REG32(SPI + 0x00000000U)
#define SPI_CTL1(spix)    REG32(SPI + 0x00000004U)
#define SPI_STAT(spix)    REG32(SPI + 0x00000008U)
#define SPI_DATA(spix)    REG32(SPI + 0x0000000CU)
#define SPI_CRCPOLY(spix) REG32(SPI + 0x00000010U)
#define SPI_RCRC(spix)    REG32(SPI + 0x00000014U)
#define SPI_TCRC(spix)    REG32(SPI + 0x00000018U)
#define SPI_QCTL(spix)    REG32(SPI + 0x00000080U)

#endif /* CONFIG_SPI_GD32 */

/*
 * EXTI pin mapping.
 * GD32M53x pin-to-EXTI mapping is non-linear: the EXTI line number does not
 * equal the pin number, and the EXTISS selection value does not equal the
 * port index. The lookup table lives in soc.c.
 */
#ifdef CONFIG_GPIO_GD32

/** Returned when the pin has no EXTI mapping. */
#define GD32M53X_EXTI_INVALID 0xFFU

/** Extract the EXTI line number from an encoded mapping value. */
#define GD32M53X_EXTI_LINE(v) ((uint8_t)((v) >> 4))
/** Extract the EXTISS selection value from an encoded mapping value. */
#define GD32M53X_EXTI_SEL(v)  ((uint8_t)((v) & 0x0FU))

/**
 * @brief Look up the EXTI mapping for a GPIO pin.
 *
 * @param gpio_base GPIO port base address (GPIOA..GPION).
 * @param pin Pin number within the port.
 *
 * @return (line << 4) | extiss_sel, or GD32M53X_EXTI_INVALID if the pin has
 *         no EXTI mapping.
 */
uint8_t gd32m53x_exti_encode_get(uint32_t gpio_base, uint8_t pin);

#endif /* CONFIG_GPIO_GD32 */

#endif /* ZEPHYR_SOC_GD_GD32_GD32M531_SOC_H_ */
