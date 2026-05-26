#pragma once

// Waveshare ESP32-S3-Touch-AMOLED-2.16
// https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm
// Display: CO5300 (QSPI AMOLED), 466x466 active area.
// Touch:   CST92xx series (I2C), address 0x5A.
// Pins below mirror Waveshare's official Arduino examples:
// https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16

#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK  38
#define LCD_RESET  2
#define LCD_CS    12
#define LCD_WIDTH  466
#define LCD_HEIGHT 466

// I2C (shared by touch + IMU + AXP2101 PMIC)
#define IIC_SDA 15
#define IIC_SCL 14

// Touch (CST92xx)
#define TP_INT 11
#define TP_RST  2  // Shared with LCD_RESET on this board.
