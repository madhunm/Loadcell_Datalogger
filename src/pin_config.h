/**
 * @file pin_config.h
 * @brief GPIO Pin Definitions for ESP32-S3 Loadcell Data Logger
 * 
 * Hardware connections:
 * - MAX11270: 24-bit ADC for loadcell (SPI)
 * - LSM6DSV: IMU accelerometer/gyroscope (I2C)
 * - RX8900CE: RTC with 1Hz sync output (I2C)
 * - SD Card: SDMMC 4-bit interface
 * - NeoPixel: Status LED
 * - Button: Logging control
 */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#include <Arduino.h>

// ============================================================================
// USER INTERFACE
// ============================================================================

/** @brief Logging start/stop button (active HIGH, external pulldown) */
#define PIN_LOG_BUTTON          GPIO_NUM_2

/** @brief NeoPixel status LED data pin */
#define PIN_NEOPIXEL            GPIO_NUM_21

// ============================================================================
// SDMMC INTERFACE (4-bit mode)
// ============================================================================

/** @brief SD card clock */
#define PIN_SD_CLK              GPIO_NUM_4

/** @brief SD card command line */
#define PIN_SD_CMD              GPIO_NUM_5

/** @brief SD card data line 0 */
#define PIN_SD_D0               GPIO_NUM_6

/** @brief SD card data line 1 */
#define PIN_SD_D1               GPIO_NUM_7

/** @brief SD card data line 2 */
#define PIN_SD_D2               GPIO_NUM_8

/** @brief SD card data line 3 */
#define PIN_SD_D3               GPIO_NUM_9

/** @brief SD card detect (active LOW: LOW=card present, HIGH=no card) */
#define PIN_SD_CD               GPIO_NUM_10

// ============================================================================
// MAX11270 ADC (SPI Interface for Loadcell)
// ============================================================================

/** @brief ADC SPI MISO (data from ADC) */
#define PIN_ADC_MISO            GPIO_NUM_12

/** @brief ADC SPI MOSI (data to ADC) */
#define PIN_ADC_MOSI            GPIO_NUM_13

/** @brief ADC SYNC signal */
#define PIN_ADC_SYNC            GPIO_NUM_14

/** @brief ADC reset (active LOW) */
#define PIN_ADC_RSTB            GPIO_NUM_15

/** @brief ADC data ready (active LOW: LOW=data ready) */
#define PIN_ADC_RDYB            GPIO_NUM_16

/** @brief ADC chip select (active LOW) */
#define PIN_ADC_CS              GPIO_NUM_17

/** @brief ADC SPI clock */
#define PIN_ADC_SCK             GPIO_NUM_18

// ============================================================================
// USB (Native)
// ============================================================================

/** @brief USB D- (directly connected to USB connector) */
#define PIN_USB_DM              GPIO_NUM_19

/** @brief USB D+ (directly connected to USB connector) */
#define PIN_USB_DP              GPIO_NUM_20

// ============================================================================
// RX8900CE RTC
// ============================================================================

/** @brief RTC 1Hz square wave output (for timestamp discipline) */
#define PIN_RTC_FOUT            GPIO_NUM_33

/** @brief RTC interrupt output (active LOW) */
#define PIN_RTC_INT             GPIO_NUM_34

// ============================================================================
// LSM6DSV IMU
// ============================================================================

/** @brief IMU interrupt 1 */
#define PIN_IMU_INT1            GPIO_NUM_39

/** @brief IMU interrupt 2 */
#define PIN_IMU_INT2            GPIO_NUM_40

// ============================================================================
// I2C BUS (Shared: RTC + IMU + others)
// ============================================================================

/** @brief I2C data line (external pull-up to 3.3V required) */
#define PIN_I2C_SDA             GPIO_NUM_41

/** @brief I2C clock line (external pull-up to 3.3V required) */
#define PIN_I2C_SCL             GPIO_NUM_42

// ============================================================================
// I2C DEVICE ADDRESSES
// ============================================================================

/** @brief RX8900CE RTC I2C address */
#define I2C_ADDR_RX8900CE       0x32

/** @brief LSM6DSV IMU I2C address (SA0=0) */
#define I2C_ADDR_LSM6DSV        0x6A

/** @brief LSM6DSV IMU alternate address (SA0=1) */
#define I2C_ADDR_LSM6DSV_ALT    0x6B

// ============================================================================
// SPI CONFIGURATION
// ============================================================================

/** @brief MAX11270 SPI clock frequency (up to 5MHz for this ADC) */
#define ADC_SPI_FREQ_HZ         4000000

/** @brief SPI mode for MAX11270 (CPOL=0, CPHA=0) */
#define ADC_SPI_MODE            SPI_MODE0

// ============================================================================
// I2C CONFIGURATION
// ============================================================================

/** @brief I2C bus frequency (400kHz fast mode) */
#define I2C_FREQ_HZ             400000

// ============================================================================
// TIMING CONSTANTS
// ============================================================================

/** @brief Button debounce time in milliseconds */
#define BUTTON_DEBOUNCE_MS      50

/** @brief NeoPixel count (single LED) */
#define NEOPIXEL_COUNT          1

// ============================================================================
// ACTIVE LEVELS
// ============================================================================

/** @brief Button active level (HIGH when pressed) */
#define BUTTON_ACTIVE_LEVEL     HIGH

/** @brief SD card detect active level (LOW when card present) */
#define SD_CD_ACTIVE_LEVEL      LOW

/** @brief ADC reset active level (LOW to reset) */
#define ADC_RSTB_ACTIVE_LEVEL   LOW

/** @brief ADC data ready active level (LOW when data ready) */
#define ADC_RDYB_ACTIVE_LEVEL   LOW

/** @brief ADC chip select active level (LOW to select) */
#define ADC_CS_ACTIVE_LEVEL     LOW

/** @brief RTC interrupt active level (LOW when interrupt) */
#define RTC_INT_ACTIVE_LEVEL    LOW

#endif // PIN_CONFIG_H

