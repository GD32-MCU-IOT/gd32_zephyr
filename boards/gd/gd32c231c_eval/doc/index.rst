.. _gd32c231c_eval:

GD32C231C-EVAL
##############

Overview
********

The GD32C231C-EVAL evaluation board is a low-cost development platform for
GigaDevice GD32C231C microcontroller series based on ARM Cortex-M23 core.

The GD32C231C8 features:

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

- MCU: GD32C231C8T6
- Crystal: 8MHz external crystal for HXTAL
- LED: User LED connected to PA5
- Debug: SWD interface

Connections and IOs
===================

LED
---

- LED0 (User) = PA5

UART
----

- USART0_TX = PA9
- USART0_RX = PA10

Programming and Debugging
*************************

Flashing
========

The GD32C231C-EVAL board can be flashed using OpenOCD or J-Link.

Using OpenOCD:

.. code-block:: console

   west flash

Using J-Link:

.. code-block:: console

   west flash -r jlink

Debugging
=========

You can debug an application in the usual way. Here is an example for the
:ref:`hello_world` application.

.. code-block:: console

   west debug
