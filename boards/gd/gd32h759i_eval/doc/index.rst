.. zephyr:board:: gd32h759i_eval:

GigaDevice GD32H759I-EVAL
#########################

Overview
********

The GD32H759I-EVAL board is a hardware platform that enables prototyping
on GD32H759I Cortex-M7 Stretch Performance MCU.

The GD32H759IMK6 features a single-core ARM Cortex-M7 MCU which can run up
to 600 MHz with 3072kiB of Flash, 1024kiB of SRAM and 176 PINs.

.. image:: img/gd32H759I_eval.jpg
     :align: center
     :alt: gd32H759I_eval


Hardware
********

- GD32H759IMK6 MCU
- ATMLH7482ECL EEPROM or AT24C02C 2Kb EEPROM
- GD25QX512ME 512Mbit SPI and OCSPI NOR Flash
- QSPI_LCD
- Micron MT48LC16M16A2P-6AIT 256Mbit SDRAM
- 2 x User LEDs
- 3 x User Push buttons
- 1 x USART (CH340E at J51 connector)
- 1 x POT connected to an ADC input
- Micro SD Card Interface
- 2 X USB HS connector
- 2 x CAN
- 4.3" RGB-LCD (480x272)
- OV2640 Digital Camera
- GD-Link on board programmer
- J-Link/JTAG connector

For more information about the GD32H759IM SoC and GD32H759I-EVAL board:

- `GigaDevice Cortex-M7 Stretch Performance SoC Website`_
- `GD32H759 Datasheet`_
- `GD32H7xx User Manual`_
- `GD32H759I-EVAL User Manual`_

Supported Features
==================

The board configuration supports the following hardware features:

.. list-table::
   :header-rows: 1

   * - Peripheral
     - Kconfig option
     - Devicetree compatible
   * - EXTI
     - :kconfig:option:`CONFIG_GD32_EXTI`
     - :dtcompatible:`gd,gd32-exti`
   * - GPIO
     - :kconfig:option:`CONFIG_GPIO`
     - :dtcompatible:`gd,gd32-gpio`
   * - NVIC
     - N/A
     - :dtcompatible:`arm,v7m-nvic`
   * - PWM
     - :kconfig:option:`CONFIG_PWM`
     - :dtcompatible:`gd,gd32-pwm`
   * - CAN
     - :kconfig:option:`CONFIG_CAN`
     - :dtcompatible:`gd,gd32-can`
   * - DMA
     - :kconfig:option:`CONFIG_DMA`
     - :dtcompatible:`gd,gd32-dma`
   * - SYSTICK
     - N/A
     - N/A
   * - USART
     - :kconfig:option:`CONFIG_SERIAL`
     - :dtcompatible:`gd,gd32-usart`
   * - ADC
     - :kconfig:option:`CONFIG_ADC`
     - :dtcompatible:`gd,gd32-adc`
   * - SPI
     - :kconfig:option:`CONFIG_SPI`
     - :dtcompatible:`gd,gd32-spi`

Serial Port
===========

The GD32H759I-EVAL board has one serial communication port. The default port
is USART1 with TX connected at PA2 and RX at PA3

Programming and Debugging
*************************

Before programming your board make sure to configure boot and serial jumpers
as follows:

- J2/3: Select 2-3 for both (boot from user memory)
- J51: Select 1-2 position (labeled as ``USART``)

Using GD-Link
=============

The GD32H759I-EVAL includes an onboard programmer/debugger (GD-Link) which
allows flash programming and debugging over USB. There is also a JTAG header
(J1) which can be used with tools like Segger J-Link.

#. Build the Zephyr kernel and the :ref:`hello_world` sample application:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32h7xx_eval
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
      :board: gd32h7xx_eval
      :goals: flash
      :compact:

   You should see "Hello World! gd32h7xx_eval" in your terminal.

#. To debug an image:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32h7xx_eval
      :goals: debug
      :compact:


.. _GigaDevice SoC Website:
   https://www.gigadevice.com.cn
