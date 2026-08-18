.. zephyr:board:: gd32e517z_eval

GigaDevice GD32E517Z-EVAL
##########################

Overview
********

The GD32E517Z-EVAL board is a hardware platform that enables prototyping
on GD32E517ZE Cortex-M33 Mainstream MCU.

The GD32E517ZE features a single-core ARM Cortex-M33 MCU which can run up
to 180 MHz with 512kiB of Flash, 128kiB of SRAM and 144 PINs.

Hardware
********

- GD32E517ZET6 MCU
- 2Kb EEPROM (AT24C02 on I2C0, I2C1 and I2C2 buses)
- 16Mbit SPI NOR Flash (GD25Q16 on SPI1)
- 4 x User LEDs
- 5 x User Push buttons
- 2 x USART (USART0 with DMA, USART5)
- 3 x I2C (I2C0 with DMA, I2C1, I2C2 with DMA)
- 3 x SPI (SPI0, SPI1 with DMA, SPI2)
- GD-Link on board programmer
- J-Link/JTAG connector

For more information about the GD32E517 SoC and GD32E517Z-EVAL board:

- `GigaDevice Cortex-M33 Mainstream MCU Website`_
- `GD32E517xx Datasheet`_
- `GD32E51x User Manual`_

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
   * - DMA
     - :kconfig:option:`CONFIG_DMA`
     - :dtcompatible:`gd,gd32-dma`
   * - SPI
     - :kconfig:option:`CONFIG_SPI`
     - :dtcompatible:`gd,gd32-spi`
   * - I2C
     - :kconfig:option:`CONFIG_I2C`
     - :dtcompatible:`gd,gd32-i2c`
   * - PWM
     - :kconfig:option:`CONFIG_PWM`
     - :dtcompatible:`gd,gd32-pwm`
   * - Counter
     - :kconfig:option:`CONFIG_COUNTER`
     - :dtcompatible:`gd,gd32-counter`
   * - EEPROM
     - :kconfig:option:`CONFIG_EEPROM`
     - :dtcompatible:`atmel,at24`

Serial Port
===========

The GD32E517Z-EVAL board has two serial communication ports. The default
console port is USART0 with TX connected at PA9 and RX at PA10. A second
USART (USART5) is also available for async API testing.

Connect to the USB-to-Serial converter for serial console access.

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
     - PG10
     - High
   * - LED2
     - PG11
     - High
   * - LED3
     - PG12
     - High
   * - LED4
     - PG13
     - High

Button Connections
------------------

.. list-table::
   :header-rows: 1

   * - Name
     - GPIO Pin
     - Description
   * - KEY_A
     - PA0
     - User button (active low)
   * - KEY_B
     - PC13
     - User button (active low)
   * - KEY_C
     - PF13
     - User button (active low)
   * - KEY_D
     - PF14
     - User button (active low)
   * - KEY_CET
     - PF15
     - User button (active low)

I2C Connections
--------------

.. list-table::
   :header-rows: 1

   * - Bus
     - EEPROM
     - SCL
     - SDA
   * - I2C0
     - AT24C02 (256 bytes)
     - PB6
     - PB7
   * - I2C1
     - AT24C02 (256 bytes)
     - PB10
     - PB11
   * - I2C2
     - AT24C02 (256 bytes)
     - PA8
     - PC9

SPI Connections
---------------

.. list-table::
   :header-rows: 1

   * - Bus
     - Device
     - CS
     - Description
   * - SPI1
     - GD25Q16 (16Mbit SPI NOR)
     - PB12
     - SPI NOR Flash

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Before programming your board make sure to configure boot and serial jumpers
as follows:

- Boot jumper: Select boot from Flash (normal boot mode)

Using GD-Link
=============

The GD32E517Z-EVAL includes an onboard programmer/debugger (GD-Link) which
allows flash programming and debugging over USB. There is also a JTAG header
which can be used with tools like Segger J-Link.

#. Build the Zephyr kernel and the :zephyr:code-sample:`hello_world` sample
   application:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32e517z_eval
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
      :board: gd32e517z_eval
      :goals: flash
      :compact:

   You should see "Hello World! gd32e517z_eval" in your terminal.

#. To debug an image:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32e517z_eval
      :goals: debug
      :compact:
