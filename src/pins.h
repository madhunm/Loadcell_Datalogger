#ifndef PINS_H
#define PINS_H

#ifdef __cplusplus

// User-provided ESP32-S3 pin mapping (single source of truth)

// Button + NeoPixel
static constexpr int PIN_LOG_BUTTON = 2;
static constexpr int PIN_NEOPIXEL   = 21;  // WS2812 single LED

// MAX11270 SPI pins (ADC CLK tied to GND -> internal clock)
static constexpr int PIN_ADC_MISO = 12;
static constexpr int PIN_ADC_MOSI = 13;
static constexpr int PIN_ADC_SYNC = 14;
static constexpr int PIN_ADC_RSTB = 15;
static constexpr int PIN_ADC_RDYB = 16;
static constexpr int PIN_ADC_CS   = 17;
static constexpr int PIN_ADC_SCK  = 18;

// SD_MMC 4-bit
static constexpr int PIN_SD_CLK = 4;
static constexpr int PIN_SD_CMD = 5;
static constexpr int PIN_SD_D0  = 6;
static constexpr int PIN_SD_D1  = 7;
static constexpr int PIN_SD_D2  = 8;
static constexpr int PIN_SD_D3  = 9;
static constexpr int PIN_SD_CD  = 10;  // optional card-detect

// I2C bus (fuel gauge, IMU, RTC)
static constexpr int PIN_I2C_SDA = 41;
static constexpr int PIN_I2C_SCL = 42;

// IMU interrupts
static constexpr int PIN_IMU_INT1 = 39;
static constexpr int PIN_IMU_INT2 = 40;

// RTC
static constexpr int PIN_RTC_FOUT = 33;
static constexpr int PIN_RTC_INT  = 34;

#endif
#endif
