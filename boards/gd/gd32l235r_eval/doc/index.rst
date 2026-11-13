.. zephyr:board:: gd32l235r_eval

Overview
********

The GD32L235R-EVAL board is a hardware platform that enables design and debug
of the GigaDevice GD32L235 Cortex-M23 Low Power MCU.

The GD32L235RBT6 features a single-core ARM Cortex-M23 MCU with 128kB of Flash
and 24kB of SRAM.

Hardware
********

- GD32L235RBT6 MCU
- 2 x User LEDs (PA5, PB12)
- 2 x User Push buttons (Wakeup PA0, Tamper PC13)
- 1 x USART (USART1, console)
- GD-Link on board programmer
- J-Link/SWD connector

For more information about the GD32L235 SoC and GD32L235R-EVAL board:

- `GigaDevice Cortex-M23 Low Power SoC Website`_

Supported Features
==================

.. zephyr:board-supported-hw::

Serial Port
===========

The GD32L235R-EVAL board has one serial communication port. The default port
is USART1 with TX connected at PA2 and RX at PA3.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Using pyOCD
===========

The GD32L235R-EVAL includes an onboard programmer/debugger (GD-Link) which
allows flash programming and debugging over USB. There is also a SWD header
which can be used with tools like Segger J-Link.

#. Build the Zephyr kernel and the :zephyr:code-sample:`hello_world` sample application:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32l235r_eval
      :goals: build
      :compact:

#. Run your favorite terminal program to listen for output. On Linux the
   terminal should be something like ``/dev/ttyUSB0``. For example:

   .. code-block:: console

      minicom -D /dev/ttyUSB0 -o

   The -o option tells minicom not to send the modem initialization
   string. Connection should be configured as follows:

      - Speed: 115200
      - Data: 8 bits
      - Parity: None
      - Stop bits: 1

#. To flash an image:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32l235r_eval
      :goals: flash
      :compact:

   You should see "Hello World! gd32l235r_eval/gd32l235" in your terminal.

#. To debug an image:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32l235r_eval
      :goals: debug
      :compact:

.. _GigaDevice Cortex-M23 Low Power SoC Website:
   https://www.gigadevice.com/products/microcontrollers/gd32/arm-cortex-m23/low-power-line/
