.. zephyr:code-sample:: gd32-dual-adc
   :name: GD32 Dual ADC (GD32F503V)
   :relevant-api: adc_interface

   Cyclically sample two ADCs: ADC1 channel 0 (PA0) and ADC0 channel 13 (PC3).

Overview
********

This sample reads two analog inputs on a GD32F503V using both ADC units:

* ADC1 channel 0 -> PA0
* ADC0 channel 13 -> PC3

The board default pinctrl configures ADC1 on PA1 to avoid conflict with
``key_a`` (PA0).  The sample ships an **overlay**
(``boards/gd32f503v_eval.overlay``) that overrides the ADC1 pinctrl to PA0
and adds channel 0, so the sample uses PA0 + PC3 as documented.

Conversions use software-triggered, polled reads (no DMA, no interrupt) which
is the reliable path on the GD32F50x ADC. The on-board ``led0`` toggles each
loop as an alive indicator, and results are printed over the console.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/adc/gd32_dual_adc
   :board: gd32f503v_eval
   :goals: build flash
   :compact:

Expected console output (values depend on the applied input voltages)::

   setup adc1 ch0 rc=0
   setup adc0 ch13 rc=0
   PA0=1234  PC3=2345
   ...
