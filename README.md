# SD card demo project

Demo project that shows SD card and file system usage.

The program performs for FatFS and LittleFS file systems the following actions:

- format SD card
- shows file creation speed
- shows linear read/write speed

The result is printed to default UART device.

## Requirements

This project is made for [STM32Discovery](https://www.st.com/en/evaluation-tools/stm32f3discovery.html)
with external sd card that uses pins `PB_12`, `PB_13`, `PB_14`, `PB_15`.

If you want to use other board, please adjust files `.mbed` and `mbed_app.json` according your settings.
