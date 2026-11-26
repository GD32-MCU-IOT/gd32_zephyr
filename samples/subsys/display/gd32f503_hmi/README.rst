GD32F503V-EVAL HMI
##################

This sample uses the GD32F503V-EVAL ILI9320 display and XPT2046 touch panel.
It displays a white screen at startup, then opens four LVGL pages. The GPIO
page mirrors the three LEDs and keys, the voltage page reads the potentiometer
and internal ADC channels, and the EEPROM page provides probe, read and write
operations. The Write button modifies the selected EEPROM byte.

The voltage and temperature readings are estimates based on a nominal 1.2 V
internal reference and nominal temperature sensor characteristics. If the
reference reading is invalid, the voltage calculation uses 3.3 V instead.

Build on a Zephyr installation whose modules match its west manifest::

   west build -p always -b gd32f503v_eval samples/subsys/display/gd32f503_hmi

The sample uses PA0, PA1 and PC13 for the keys, PC0 for the potentiometer,
and the board's I2C0 EEPROM. ADC1 is disabled because it uses PA1 in the board
default configuration. Display orientation, touch calibration, ADC readings,
and EEPROM behavior must be checked on the actual board.
