.. zephyr:board:: gd32e230c_eval

Overview
********

The GD32E230C-EVAL board is a hardware platform for prototyping with the
GigaDevice GD32E230C8T6 Cortex-M23 MCU.

The Zephyr board configuration enables the GD32E230C8 SoC, USART0 console,
GPIO, pin control, and clock control support.

Hardware
********

The following hardware is described by the current Zephyr board files:

- GD32E230C8T6 MCU at 72 MHz
- 64 KiB flash
- 8 KiB SRAM
- LED1 connected to PA8
- LED2 connected to PA11
- LED3 connected to PA12
- LED4 connected to PA15
- WAKEUP key connected to PA0
- TAMPER key connected to PC13
- USART0 console with TX on PA9 and RX on PA10
- On-board GD-Link (CMSIS-DAP) debug probe

Supported Features
==================

.. zephyr:board-supported-hw::

Serial Port
===========

The default serial console is USART0. The board pin control configuration
connects USART0 TX to PA9 and USART0 RX to PA10. USART0 is available on the
host through the on-board CH340E USB-to-serial converter. The default baud
rate is 115200 bit/s.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

``west flash`` uses pyOCD with the on-board GD-Link by default. The GD32E23x DFP
pack is provided by the GigaDevice HAL module in
``modules/hal/gigadevice/gd32e23x/support/``. A J-Link runner is also
configured for use with an external J-Link probe.

Build the Zephyr kernel and the :zephyr:code-sample:`hello_world` sample
application:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32e230c_eval
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
   :board: gd32e230c_eval
   :goals: flash
   :compact:

You should see "Hello World! gd32e230c_eval/gd32e230" in your terminal.

To debug an image:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: gd32e230c_eval
   :goals: debug
   :compact:
