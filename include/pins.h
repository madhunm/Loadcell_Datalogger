/**
 * @file pins.h
 * @brief GPIO pin definitions for ESP32-S3 Loadcell Datalogger
 * @details This file centralizes all GPIO pin assignments for the system.
 *          All pin numbers should be defined here and referenced via these
 *          constants throughout the codebase. Do NOT hardcode pin numbers.
 * 
 * @author Loadcell Datalogger Project
 * @date December 2024
 */

#pragma once
#include <Arduino.h>
#include "driver/gpio.h"

// ============================================================================
// BUTTONS
// ============================================================================

/**
 * @brief LOG_START button pin
 * @details Button has external pulldown to GND; button connects to 3.3V when pressed.
 *          Active HIGH logic (HIGH = pressed, LOW = not pressed).
 */
static const gpio_num_t PIN_LOGSTART_BUTTON = GPIO_NUM_2;   // IO2

// ============================================================================
// SDMMC (4-bit SD card interface)
// ============================================================================

/** @brief SD card clock pin (CLK) */
static const gpio_num_t PIN_SD_CLK   = GPIO_NUM_4;

/** @brief SD card command pin (CMD) */
static const gpio_num_t PIN_SD_CMD   = GPIO_NUM_5;

/** @brief SD card data line 0 (D0) */
static const gpio_num_t PIN_SD_D0    = GPIO_NUM_6;

/** @brief SD card data line 1 (D1) */
static const gpio_num_t PIN_SD_D1    = GPIO_NUM_7;

/** @brief SD card data line 2 (D2) */
static const gpio_num_t PIN_SD_D2    = GPIO_NUM_8;

/** @brief SD card data line 3 (D3) */
static const gpio_num_t PIN_SD_D3    = GPIO_NUM_9;

/** @brief SD card detect pin (CD) - Active LOW (HIGH = no card, LOW = card present) */
static const gpio_num_t PIN_SD_CD    = GPIO_NUM_10;

// ============================================================================
// MAX11270 (24-bit SPI ADC for loadcell)
// ============================================================================

/** @brief ADC SPI Master In Slave Out (MISO) - data from ADC to ESP32 */
static const gpio_num_t PIN_ADC_MISO = GPIO_NUM_12;

/** @brief ADC SPI Master Out Slave In (MOSI) - data from ESP32 to ADC */
static const gpio_num_t PIN_ADC_MOSI = GPIO_NUM_13;

/** @brief ADC SYNC pin - synchronization control */
static const gpio_num_t PIN_ADC_SYNC = GPIO_NUM_14;

/** @brief ADC reset pin (RSTB) - Active LOW reset */
static const gpio_num_t PIN_ADC_RSTB = GPIO_NUM_15;

/** @brief ADC data ready pin (RDYB) - Active LOW (LOW = data ready) */
static const gpio_num_t PIN_ADC_RDYB = GPIO_NUM_16;

/** @brief ADC chip select pin (CS) - Active LOW */
static const gpio_num_t PIN_ADC_CS   = GPIO_NUM_17;

/** @brief ADC SPI clock pin (SCK) */
static const gpio_num_t PIN_ADC_SCK  = GPIO_NUM_18;

// ============================================================================
// USB (native full-speed USB)
// ============================================================================

/** @brief USB data minus (DM) pin */
static const gpio_num_t PIN_USB_DM   = GPIO_NUM_19;

/** @brief USB data plus (DP) pin */
static const gpio_num_t PIN_USB_DP   = GPIO_NUM_20;

// ============================================================================
// STATUS INDICATORS
// ============================================================================

/** @brief NeoPixel LED data pin - WS2812B or compatible RGB LED */
static const gpio_num_t PIN_NEOPIXEL = GPIO_NUM_21;

// ============================================================================
// RTC (RX8900CE real-time clock)
// ============================================================================

/** @brief RTC frequency output pin (FOUT) - 1 Hz square wave output */
static const gpio_num_t PIN_RTC_FOUT = GPIO_NUM_33;

/** @brief RTC interrupt pin (INT) - Active LOW interrupt output */
static const gpio_num_t PIN_RTC_INT  = GPIO_NUM_34;

// ============================================================================
// IMU (LSM6DSV16X accelerometer/gyroscope) interrupts
// ============================================================================

/** @brief IMU interrupt 1 pin (INT1) - configurable interrupt output */
static const gpio_num_t PIN_IMU_INT1 = GPIO_NUM_39;

/** @brief IMU interrupt 2 pin (INT2) - configurable interrupt output */
static const gpio_num_t PIN_IMU_INT2 = GPIO_NUM_40;

// ============================================================================
// I2C bus (shared by RTC, IMU, and other I2C devices)
// ============================================================================

/** @brief I2C data line (SDA) - requires 4.7kΩ pull-up to 3.3V */
static const gpio_num_t PIN_I2C_SDA  = GPIO_NUM_41;

/** @brief I2C clock line (SCL) - requires 4.7kΩ pull-up to 3.3V */
static const gpio_num_t PIN_I2C_SCL  = GPIO_NUM_42;
