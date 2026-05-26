# Sand (Multi-Task) - Waveshare ESP32-S3 Touch AMOLED 2.16"

Port of the [Sand (Multi-Task) 4.3 Inch project](../sand-multi-task-4_3inch) targeting the [Waveshare ESP32-S3-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm) board.

## Hardware

- MCU: ESP32-S3R8 (240 MHz, 8 MB OPI PSRAM)
- Flash: 16 MB
- Display: 2.16" AMOLED, CO5300 driver over QSPI, 466 x 466 active pixels (panel is square with rounded corners; firmware draws into the full 466x466 framebuffer)
- Touch: CST92xx series capacitive (I2C @ 0x5A)
- PMIC: AXP2101 (not actively used by this firmware - display rails come up on default boot)
- IMU: QMI8658 (unused here)

> The product page lists the display as 2.16" - the active matrix resolution from the Waveshare reference Arduino code is **466 x 466**, not 480 x 480. The CO5300 panel supports up to 480 x 480, but Waveshare wires it to 466 x 466. This firmware uses 466 x 466 to match the official examples.

## Differences from the 4.3" port

- Switched from LovyanGFX (RGB parallel) to [Arduino_GFX_Library](https://github.com/moononournation/Arduino_GFX) with the built-in CO5300 panel + ESP32 QSPI bus driver.
- Touch goes through [SensorLib](https://github.com/lewisxhe/SensorLib)'s `TouchDrvCST92xx` (I2C) instead of LovyanGFX's `Touch_GT911`.
- Colors are precomputed into RGB565 per draw call (Arduino_GFX does not have a "current pen color" the way LovyanGFX does).
- Touch reset line (`TP_RST`) and LCD reset line are shared on this board, so touch is initialized before the display.

The multi-tasking sand simulation logic itself is unchanged from the 4.3" version.

## Building

```
pio run -e sand-multi-task-waveshare-esp32-s3-2_16inch -t upload
```

Or set `default_env = sand-multi-task-waveshare-esp32-s3-2_16inch` in [platformio.ini](../../platformio.ini).
