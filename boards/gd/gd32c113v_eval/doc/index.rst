.. _gd32c113v_eval:

GD32C113V-EVAL
##############

Overview
********

The GD32C113V-EVAL board is a hardware platform for GigaDevice's Arm
Cortex-M4F GD32C113VBT6 MCU. It provides comprehensive test points
and various peripherals.

The GD32C113VBT6 features a single-core Arm Cortex-M4F MCU which can run up
to 120 MHz with flash memory of 128 kB and SRAM of 32 kB. It provides up to
80 GPIOs, 2 x 12-bit ADCs, 2 x 12-bit DACs, 10 x general 16-bit timers,
2 x 16-bit PWM advanced timers, 2 x 16-bit basic timers, 3 x SPIs, 2 x I2Cs,
3 x USARTs, 2 x UARTs, 1 x USBFS and 2 x CANs.

Hardware
********

- GD32C113VBT6 MCU
- USB OTG Full-Speed connector (Mini USB)
- 4 user LEDs
- 3 user buttons (Wakeup, Tamper, User)
- 8 MHz high speed crystal oscillator
- 32.768 kHz low speed crystal oscillator
- GD-Link on board debugger
- JTAG/SWD debug interface

Supported Features
==================

The board configuration supports the following hardware features:

+-----------+------------+----------------------+
| Interface | Controller | Driver/Component     |
+===========+============+======================+
| NVIC      | on-chip    | nested vectored      |
|           |            | interrupt controller |
+-----------+------------+----------------------+
| SYSTICK   | on-chip    | system clock         |
+-----------+------------+----------------------+
| USART     | on-chip    | serial port          |
+-----------+------------+----------------------+
| GPIO      | on-chip    | gpio                 |
+-----------+------------+----------------------+
| PINMUX    | on-chip    | pinctrl              |
+-----------+------------+----------------------+
| I2C       | on-chip    | i2c                  |
+-----------+------------+----------------------+
| SPI       | on-chip    | spi                  |
+-----------+------------+----------------------+
| DMA       | on-chip    | dma                  |
+-----------+------------+----------------------+

Serial Port
===========

The GD32C113V-EVAL board has one serial communication port. The default port
is USART0 with TX on PA9 and RX on PA10.

Programming and Debugging
*************************

The GD32C113V-EVAL board can be programmed and debugged by GD-Link, J-Link or
other debuggers supporting SWD/JTAG interface.

Building & Flashing
===================

Here is an example for the :zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32c113v_eval
   :goals: build flash

Debugging
=========

You can debug an application with GD-Link or J-Link. Here is an example:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32c113v_eval
   :goals: debug
