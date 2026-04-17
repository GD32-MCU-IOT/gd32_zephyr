.. zephyr:board:: gd32h759i_start

GigaDevice GD32H759I-START
##########################

Overview
********

The GD32H759I-START board is a hardware platform that enables prototyping
on GD32H759I Cortex-M7 Stretch Performance MCU.

The GD32H759IMT6 features a single-core ARM Cortex-M7 MCU which can run up
to 600 MHz with 3072kiB of Flash, 1024kiB of SRAM and 176 PINs.

Hardware
********

- GD32H759IMT6 MCU
- 4 x User LEDs
- 1 x User Push button
- 1 x USART (CH340E USB-to-Serial at Mini-USB connector CN1)
- GD-Link on board programmer
- J-Link/JTAG connector

For more information about the GD32H759IM SoC and GD32H759I-START board:

- `GigaDevice Cortex-M7 Stretch Performance SoC Website`_
- `GD32H759 Datasheet`_
- `GD32H7xx User Manual`_

Supported Features
==================

The board configuration supports the following hardware features:

.. list-table::
   :header-rows: 1

   * - Peripheral
     - Kconfig option
     - Devicetree compatible
   * - GPIO
     - :kconfig:option:`CONFIG_GPIO`
     - :dtcompatible:`gd,gd32-gpio`
   * - USART
     - :kconfig:option:`CONFIG_SERIAL`
     - :dtcompatible:`gd,gd32-usart`

Serial Port
===========

The GD32H759I-START board has one serial communication port connected via
USB-to-Serial converter (CH340E). The default port is USART0 with TX
connected at PF4 and RX at PF5. Connect to Mini-USB connector CN1 for
serial console access.

Connections and IOs
===================

LED Connections
---------------

.. list-table::
   :header-rows: 1

   * - Name
     - GPIO Pin
     - Active Level
   * - LED1
     - PC9
     - Low
   * - LED2
     - PC10
     - Low
   * - LED3
     - PC11
     - Low
   * - LED4
     - PC12
     - Low

Button Connections
------------------

.. list-table::
   :header-rows: 1

   * - Name
     - GPIO Pin
     - Description
   * - K2
     - PD15
     - User button (active low with pull-up)

Programming and Debugging
*************************

Before programming your board make sure to configure boot and serial jumpers
as follows:

- JP2 BOOT: Connect to GND for normal boot from Flash

Using GD-Link
=============

The GD32H759I-START includes an integrated GD-Link adapter which
provides debugging and flash programming capabilities.

#. Build the Zephyr kernel and the :zephyr:code-sample:`blinky` sample application:

   .. zephyr-app-commands::
      :zephyr-app: samples/basic/blinky
      :board: gd32h759i_start
      :goals: build
      :compact:

#. Connect the board to your host computer using the USB cable connected to
   the GD-Link Mini-USB connector.

#. Run your favorite terminal program to listen for output. Under Linux the
   terminal should be :code:`/dev/ttyUSB0`. For example:

   .. code-block:: console

      $ minicom -D /dev/ttyUSB0 -o

   The -o option tells minicom not to send the modem initialization string.
   Connection should be configured as follows:

   - Speed: 115200
   - Data: 8 bits
   - Parity: None
   - Stop bits: 1

#. Flash the image:

   .. zephyr-app-commands::
      :zephyr-app: samples/basic/blinky
      :board: gd32h759i_start
      :goals: flash
      :compact:

   You should see LED1 (PC9) blinking.

Debugging
=========

You can debug an application with pyocd or jlink debugger.

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: gd32h759i_start
   :maybe-hierarchical: west debug
   :goals: debug
   :compact:
