.. zephyr:board:: gd32h77di_eval

Overview
********

The GD32H77DI-EVAL board is a hardware platform that enables prototyping
on the GD32H77DIWK7 Cortex-M7 high performance MCU.

The GD32H77DIWK7 features a single-core ARM Cortex-M7 MCU which can run up
to 600 MHz with flash accesses zero wait states, 2048 KiB of code NVM
(CNVM), 7680 KiB of extended code NVM (ECNVM), 768 KiB of AXI SRAM,
256 KiB of DTCM and 128 KiB of ITCM.

Hardware
********

- GD32H77DIWK7 MCU
- 2 x user LEDs
- 3 x user push buttons (WAKEUP, TAMPER, USER)
- GD-Link on board programmer
- USART0 console on PB6/PB7

Supported Features
==================

Current Zephyr support covers the minimal system only: GPIO (LEDs and
buttons), USART console and the free watchdog. Other on-chip peripherals
are described in the SoC devicetree but have not been validated on this
board yet.

.. zephyr:board-supported-hw::

Serial Port
===========

The GD32H77DI-EVAL board has one serial communication port. The default
port is USART0 with TX connected at PB6 and RX at PB7.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The board is flashed through the on-board GD-Link using pyOCD and the
GD32H77x_78x DFP pack shipped with the HAL.

Using GD-Link
=============

#. Build the Zephyr kernel and the :zephyr:code-sample:`blinky` sample application:

   .. zephyr-app-commands::
      :zephyr-app: samples/basic/blinky
      :board: gd32h77di_eval
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
      :zephyr-app: samples/basic/blinky
      :board: gd32h77di_eval
      :goals: flash
      :compact:
