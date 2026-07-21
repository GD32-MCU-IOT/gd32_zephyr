.. zephyr:board:: gd32h75ey_eval:

GigaDevice GD32H75EY-EVAL
#########################

Overview
********

The GD32H75EY-EVAL board is a hardware platform that enables prototyping
on GD32H75EYM Cortex-M7 Stretch Performance MCU.

The GD32H75EYM features a single-core ARM Cortex-M7 MCU which can run up
to 600 MHz with 3840kiB of Flash, 512kiB of AXI SRAM (plus 128kiB DTCM,
64kiB ITCM, 16kiB SRAM1 and 16kiB SRAM2) and 176 PINs.

.. image:: img/gd32h75ey_eval.webp
     :align: center
     :alt: gd32h75ey_eval


Hardware
********

- GD32H75EYM MCU
- ATMLH7482ECL EEPROM or AT24C02C 2Kb EEPROM
- GD25Q16 16Mbit SPI NOR Flash
- 3 x User LEDs
- 1 x USART (CH340E at J51 connector)
- 1 x POT connected to an ADC input
- Micro SD Card Interface
- 2 X USB HS connector
- 3 x CAN
- 4.3" RGB-LCD (480x272)
- OV2640 Digital Camera
- GD-Link on board programmer
- J-Link/JTAG connector

For more information about the GD32H75EYM SoC and GD32H75EY-EVAL board:

- `GigaDevice Cortex-M7 Stretch Performance SoC Website`_
- `GigaDevice Website`_

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
   * - I2C
     - :kconfig:option:`CONFIG_I2C`
     - :dtcompatible:`gd,gd32-i2c-v2`

Serial Port
===========

The GD32H75EY-EVAL board has one serial communication port. The default port
is USART2 with TX connected at PB10 and RX at PB11

Programming and Debugging
*************************

Before programming your board make sure to configure boot and serial jumpers
as follows:

- J2/3: Select 2-3 for both (boot from user memory)
- J51: Select 1-2 position (labeled as ``USART``)

Using GD-Link
=============

The GD32H75EY-EVAL includes an onboard programmer/debugger (GD-Link) which
allows flash programming and debugging over USB. There is also a JTAG header
(J1) which can be used with tools like Segger J-Link.

#. Build the Zephyr kernel and the :ref:`hello_world` sample application:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32h75ey_eval
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
      :board: gd32h75ey_eval
      :goals: flash
      :compact:

   You should see "Hello World! gd32h75ey_eval" in your terminal.

#. To debug an image:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: gd32h75ey_eval
      :goals: debug
      :compact:


.. _GigaDevice Cortex-M7 Stretch Performance SoC Website:
   https://www.gigadevice.com.cn

.. _GigaDevice Website:
   https://www.gigadevice.com.cn
