.. zephyr:code-sample:: gd32-usb-cdc-acm
   :name: GD32 USB CDC ACM (legacy stack)
   :relevant-api: _usb_device_core_api uart_interface

GD32 USB CDC ACM
################

Overview
********

Implements a loopback on the GD32H759I-EVAL board via USB virtual serial port (CDC ACM):
after the host enumerates the ``ttyACM*`` serial port, data written to it is echoed back
by the device. This sample is based on the Zephyr **legacy USB device stack** +
GD32 USBHS controller driver ``usb_dc_gd32.c``. Defaults to **USBHS0 + full-speed (FS)**.

Building and Flashing
*********************

Run the following commands from the ``zephyrproject/zephyr`` directory in a Linux Zephyr
environment (with Python virtual environment activated):

.. code-block:: console

   west build -p always -b gd32h759i_eval -d build_cdc_acm samples/subsys/usb/cdc_acm_gd32
   west flash

The build artifacts are ``build_cdc_acm/zephyr/zephyr.elf`` and ``zephyr.hex``. You can
also use existing GD32 flashing tools to program the device.

Functionality
*************

Connect the target USB device interface on the board to your PC:

.. code-block:: console

   ls /dev/ttyACM*
   minicom --device /dev/ttyACM0 --baudrate 115200

A new ``/dev/ttyACM*`` serial port will appear on the host. After opening the serial port
(asserting DTR), characters typed are echoed back as-is. The USB transfer of CDC ACM is
independent of the serial baud rate; ``115200`` is merely the setting for the host serial
tool.

Interface and FS/HS Switching
*****************************

The controller (USBHS0/USBHS1) and speed (FS/HS) are both selected via **device tree**,
with no need to modify the driver source code.

**Switching USBHS0 ↔ USBHS1**: Edit ``samples/subsys/usb/cdc_acm_gd32/app.overlay`` and
enable only one of the nodes (disable the other):

.. code-block:: dts

   &usbhs1 {                       /* change to &usbhs0 to switch to USBHS0 */
       status = "okay";
       cdc_acm_uart0: cdc_acm_uart0 {
           compatible = "zephyr,cdc-acm-uart";
           label = "GD32 CDC ACM";
       };
   };

* USBHS0: base address ``0x40040000``, IRQ 77
* USBHS1: base address ``0x40080000``, IRQ 175

**Switching FS ↔ HS**: Both settings must be modified **simultaneously** and kept
consistent; otherwise enumeration will fail.

1. Set the speed on the enabled node (in ``app.overlay``):

   .. code-block:: dts

      maximum-speed = "high-speed";   /* or "full-speed" */

2. Modify the library PHY selection in
   ``modules/hal/gigadevice/gd32h7xx/usbhs_library/config/usb_conf.h``:

   .. code-block:: c

      /* #define USE_USB_FS */
      #define USE_USB_HS

FS uses ``PLL1Q`` to provide the 48 MHz USB clock; HS uses the dedicated ``PLLUSBHS``,
and the driver automatically calls ``pllusb_rcu_config()``. For HS, you may need to
reallocate FIFOs in ``usb_conf.h`` according to the 480 Mbps rate. After making changes,
rebuild (``west build -p always ...``).
