.. zephyr:code-sample:: ospi-flash
   :name: GD32 OSPI NOR flash
   :relevant-api: flash_interface

   Use the flash API to interact with a GigaDevice GD32 OSPI NOR serial flash memory device.

Overview
********

This sample demonstrates using the :ref:`flash API <flash_api>` on a GigaDevice GD32
OSPI NOR serial flash memory device (:dtcompatible:`gd,gd32-ospi-nor`). It is a
dedicated, GD32-OSPI-only counterpart of :zephyr:code-sample:`spi-nor`, kept
separate so that GD32 OSPI validation does not need any extra build
arguments and does not need to share build logic with other vendors'
SPI/QSPI/OSPI flash controllers.

Building and Running
********************

The application only supports boards with a devicetree node using the
:dtcompatible:`gd,gd32-ospi-nor` compatible enabled by default (e.g.
``gd32h759i_eval``), and requires no extra build arguments:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/ospi_flash
   :board: gd32h759i_eval
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

   gd25x512me GD32 OSPI flash testing
   ==========================

   Test 1: Flash erase
   Flash erase succeeded!

   Test 2: Flash write
   Attempting to write 4 bytes
   Data read matches data written. Good!!

Memory-mapped mode
*******************

When :kconfig:option:`CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED` is enabled (the
default for this sample), the driver's ``flash_read()`` implementation
switches the OSPI controller into memory-mapped mode and reads back data
directly via CPU load instructions from the controller's memory window,
instead of issuing an indirect-mode read command. ``flash_write()`` and
``flash_erase()`` always switch the controller back to indirect mode first.

The sample's ``memory_mapped_test()`` erases and writes a region using the
normal (indirect) flash API, then verifies the contents both through
``flash_read()`` and via a direct pointer dereference into the OSPI memory
window, mirroring the vendor SDK's demo flow (octal indirect write followed
by a memory-mapped read).
