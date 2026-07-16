.. zephyr:board:: gd32m531r_eval

Overview
********

The GD32M531R-EVAL board is a hardware platform for prototyping with the
GigaDevice GD32M531RC Cortex-M33F MCU.

The Zephyr board configuration enables the GD32M531RC SoC, UART3 console,
GPIO, I2C, SPI, DMA, pin control, and clock control support.

Hardware
********

The following hardware is described by the current Zephyr board files:

- GD32M531RC MCU
- 32 KiB SRAM
- LED0 connected to PA5
- LED1 connected to PD9
- LED2 connected to PD10
- LED3 connected to PD11
- LED4 connected to PD12
- User Key connected to PA0
- UART3 console with TX on PF13 and RX on PF14
- AT24-compatible EEPROM on I2C0 at address 0x50
- GD25Q16-compatible SPI NOR flash on SPI0
- DMA support for enabled I2C, UART, and SPI peripherals

Supported Features
==================

.. zephyr:board-supported-hw::

Serial Port
===========

The default serial console is UART3. The board pin control configuration
connects UART3 TX to PF13 and UART3 RX to PF14. The default baud rate is
115200 bit/s.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

``west flash`` is currently supported through pyOCD with GD-Link. The pack file
is not stored in this repository. Before flashing, install pyOCD, connect the
GD-Link probe, and download the latest GD32M53x DFP pack from
https://www.gd32mcu.com/. GigaDevice refreshes the pack periodically, so the
version number in the filename changes over time. Simply drop the downloaded
pack into ``modules/hal/gigadevice/gd32m53x/support/``; ``board.cmake`` matches
any ``GigaDevice.GD32M53x_DFP.*.pack`` by keyword, so no filename edit is
required.

Build the Zephyr kernel and the :zephyr:code-sample:`hello_world` sample
application:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32m531r_eval
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
   :board: gd32m531r_eval
   :goals: flash
   :compact:

You should see "Hello World! gd32m531r_eval" in your terminal.

To debug an image:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32m531r_eval
   :goals: debug
   :compact:
