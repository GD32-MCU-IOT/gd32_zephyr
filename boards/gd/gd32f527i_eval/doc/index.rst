.. zephyr:board:: gd32f527i_eval

Overview
********

The GD32F527I-EVAL board is a hardware platform for prototyping with the
GigaDevice GD32F527IS Cortex-M33F MCU.

The Zephyr board configuration enables the GD32F527IS SoC, USART0 console,
GPIO, I2C, SPI, DMA, pin control, and clock control support.

Hardware
********

The following hardware is described by the current Zephyr board files:

- GD32F527IS MCU
- 512 KiB SRAM
- LED0 connected to PF7
- USART0 console with TX on PA9 and RX on PA10
- AT24-compatible EEPROM on I2C0 at address 0x50
- GD25Q16-compatible SPI NOR flash on SPI5
- DMA support for enabled I2C, USART, and SPI peripherals

Supported Features
==================

.. zephyr:board-supported-hw::

Serial Port
===========

The default serial console is USART0. The board pin control configuration
connects USART0 TX to PA9 and USART0 RX to PA10. The default baud rate is
115200 bit/s.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

``west flash`` is currently supported through pyOCD with GD-Link. The required
GD32F527 DFP pack is provided by the GigaDevice HAL module, so no extra setup is
needed beyond installing pyOCD and connecting the GD-Link probe.

Build the Zephyr kernel and the :zephyr:code-sample:`hello_world` sample
application:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32f527i_eval
   :goals: build
   :compact:

Run your favorite terminal program to listen for output. On Linux the terminal
should be something like ``/dev/ttyUSB0``. For example:

.. code-block:: console

   minicom -D /dev/ttyUSB0 -o

The -o option tells minicom not to send the modem initialization string.
Connection should be configured as follows:

- Speed: 115200
- Data: 8 bits
- Parity: None
- Stop bits: 1

To flash an image:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32f527i_eval
   :goals: flash
   :compact:

You should see "Hello World! gd32f527i_eval" in your terminal.

To debug an image:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32f527i_eval
   :goals: debug
   :compact:
