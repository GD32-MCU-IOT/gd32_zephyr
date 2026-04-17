.. zephyr:board:: gd32f303b_start

Overview
********

The GD32F303B-START board is a hardware platform for evaluating and developing
applications on the GigaDevice GD32F303CBT6 MCU.

The GD32F303CBT6 features a single-core ARM Cortex-M4F MCU running at up to
120 MHz, with 128kiB of Flash, 32kiB of SRAM, and rich timer, UART, watchdog,
PWM, ADC, and DAC peripherals.

Hardware
********

- GD32F303CBT6 MCU
- 2 x User LEDs
- 1 x User button
- 1 x USART for console
- On-board GD-Link programmer/debugger
- SWD debug interface

For more information about the GD32F303 SoC and GD32F303B-START board:

- `GigaDevice Cortex-M4 Mainstream SoC Website`_

Supported Features
==================

.. zephyr:board-supported-hw::

Serial Port
===========

The GD32F303B-START board uses USART0 as the default serial console. TX is on
PA9 and RX is on PA10 with a default baud rate of 115200.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The board supports programming and debugging with the on-board GD-Link, an
external J-Link, or the built-in ROM bootloader via ``gd32isp``.

Using GD-Link or J-Link
=======================

#. Build the Zephyr kernel and the :zephyr:code-sample:`hello_world` sample
   application:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32f303b_start
      :goals: build
      :compact:

#. Connect a serial adapter to PA9, PA10, and GND.

#. Run your favorite terminal program to listen for output. On Linux the
   terminal should be something like ``/dev/ttyUSB0``. For example:

   .. code-block:: console

      minicom -D /dev/ttyUSB0 -o

   Configure the serial connection as follows:

   - Speed: 115200
   - Data: 8 bits
   - Parity: None
   - Stop bits: 1

#. To flash an image:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32f303b_start
      :goals: flash
      :compact:

   When using J-Link, append ``--runner jlink`` after ``west flash``.

#. To debug an image:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32f303b_start
      :goals: debug
      :compact:

   When using J-Link, append ``--runner jlink`` after ``west debug``.

Using ROM bootloader
====================

The GD32F303 MCU includes a ROM bootloader which can be used with the
``gd32isp`` runner.

#. Build the application:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32f303b_start
      :goals: build
      :compact:

#. Flash the image with ``gd32isp``:

   .. code-block:: console

      west flash -r gd32isp [--port=/dev/ttyUSB0]

.. _GigaDevice Cortex-M4 Mainstream SoC Website:
   https://www.gigadevice.com/products/microcontrollers/gd32/arm-cortex-m4/mainstream-line/
