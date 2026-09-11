.. zephyr:board:: gd32f5hcr_eval

Overview
********

The GD32F5HCR-EVAL board is a hardware platform for prototyping with the
GigaDevice GD32W515 (GD32W51x_F5HC) Cortex-M33 MCU.

The Zephyr board configuration enables the GD32W51x_F5HC SoC, USART2 console,
GPIO, I2C, SPI, DMA, pin control, and clock control support.

Hardware
********

The following hardware is described by the current Zephyr board files:

- GD32W515 (GD32W51x_F5HC) MCU
- 2048 KiB internal flash
- 192 KiB SRAM
- LED0 connected to PB6
- USART2 console with TX on PB10 and RX on PB11
- AT24-compatible EEPROM on I2C1 at address 0x50
- GD25Q16-compatible SPI NOR flash on SPI0
- DMA support for enabled I2C, USART, and SPI peripherals

Supported Features
==================

.. zephyr:board-supported-hw::

Serial Port
===========

The default serial console is USART2. The board pin control configuration
connects USART2 TX to PB10 and USART2 RX to PB11. The default baud rate is
115200 bit/s.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

``west flash`` is currently supported through pyOCD with GD-Link. The required
GD32W51x_F5HC DFP pack is provided by the GigaDevice HAL module, so no extra
setup is needed beyond installing pyOCD and connecting the GD-Link probe.

Build the Zephyr kernel and the :zephyr:code-sample:`hello_world` sample
application:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32f5hcr_eval
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
   :board: gd32f5hcr_eval
   :goals: flash
   :compact:

You should see "Hello World! gd32f5hcr_eval" in your terminal.

To debug an image:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32f5hcr_eval
   :goals: debug
   :compact:
