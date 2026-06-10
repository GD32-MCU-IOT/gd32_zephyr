.. zephyr:code-sample:: gd32-pwm
   :name: GD32 PWM (GD32F503V)
   :relevant-api: pwm_interface

   Generate a 50% duty-cycle PWM on TIMER2_CH0 (PC6) and blink an LED.

Overview
********

This sample configures TIMER2 channel 0 on a GD32F503V as a PWM output on
pin PC6 using ``pwm_set_cycles()`` (raw timer ticks, prescaler applied via
the board devicetree). The on-board ``led0`` is toggled every 500 ms as an
alive indicator.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/pwm/gd32_pwm
   :board: gd32f503v_eval
   :goals: build flash
   :compact:

A 50% duty-cycle square wave is produced on PC6 and LED0 blinks.
