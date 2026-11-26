GD32H759I-EVAL HMI
##################

This sample uses the GD32H759I-EVAL 480x272 TLI LCD and GT911 capacitive touch
panel. It displays a white screen at startup, then opens four LVGL pages. The
GPIO page mirrors LED2 and the keys, the voltage page reads the potentiometer,
and the EEPROM page provides probe, read and write operations. The Write button
modifies the selected EEPROM byte.

Build on a Zephyr installation whose modules match its west manifest::

   west build -p always -b gd32h759i_eval samples/subsys/display/gd32h759_hmi

Set JP41, JP43-JP59 and JP67 to 1-2 (TLI function) and JP42 to 2-3 (USER key).

The sample uses PA0 (WAKEUP), PF8 (USER) and PC13 (TAMPER) for the keys, PA6
for LED2, PC2_C (ADC2_IN0) for the potentiometer, and the board's I2C1 EEPROM.
LED1 (PF10) is not used because PF10 is the TLI DE signal. The peripherals that
share pins with the TLI and the touch panel are disabled in the board overlay.
Display orientation, touch calibration, ADC readings, and EEPROM behavior must
be checked on the actual board.
