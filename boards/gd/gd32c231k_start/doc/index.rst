.. _gd32c231k_start:

GD32C231K-START
###############

Overview
********

The GD32C231K-START is a minimal system development board for the
GigaDevice GD32C231K microcontroller series based on ARM Cortex-M23 core.

The GD32C231K8 features:

- ARM Cortex-M23 processor at 48MHz maximum
- 64KB Flash memory
- 12KB SRAM
- Low power operation
- Multiple communication interfaces: UART, SPI, I2C
- GPIO ports
- Timer modules
- ADC and comparator

Hardware
********

- MCU: GD32C231K8T6 (LQFP32)
- Crystal: 8MHz external crystal for HXTAL
- LED: User LED connected to PA8
- Debug: SWD interface

Connections and IOs
===================

LED
---

- LED0 (User) = PA8

UART
----

- USART0_TX = PA9
- USART0_RX = PA10

Programming and Debugging
*************************

Flashing
========

The GD32C231K-START board can be flashed using OpenOCD with a CMSIS-DAP
compatible debug adapter, or using J-Link.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32c231k_start
   :goals: flash

Debugging
=========

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32c231k_start
   :goals: debug
