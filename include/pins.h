#pragma once
#include <Arduino.h>
#include "driver/gpio.h"

// --- Buttons ---
// Logstart has an external pulldown to GND; button likely connects to 3V3.
static const gpio_num_t PIN_LOGSTART_BUTTON = GPIO_NUM_2;   // IO2

// --- SDMMC (4-bit SD card) ---
static const gpio_num_t PIN_SD_CLK   = GPIO_NUM_4;
static const gpio_num_t PIN_SD_CMD   = GPIO_NUM_5;
static const gpio_num_t PIN_SD_D0    = GPIO_NUM_6;
static const gpio_num_t PIN_SD_D1    = GPIO_NUM_7;
static const gpio_num_t PIN_SD_D2    = GPIO_NUM_8;
static const gpio_num_t PIN_SD_D3    = GPIO_NUM_9;
static const gpio_num_t PIN_SD_CD    = GPIO_NUM_10;

// --- MAX11270 (external SPI ADC) ---
static const gpio_num_t PIN_ADC_MISO = GPIO_NUM_12;
static const gpio_num_t PIN_ADC_MOSI = GPIO_NUM_13;
static const gpio_num_t PIN_ADC_SYNC = GPIO_NUM_14;
static const gpio_num_t PIN_ADC_RSTB = GPIO_NUM_15;
static const gpio_num_t PIN_ADC_RDYB = GPIO_NUM_16;
static const gpio_num_t PIN_ADC_CS   = GPIO_NUM_17;
static const gpio_num_t PIN_ADC_SCK  = GPIO_NUM_18;

// --- USB (native FS USB) ---
static const gpio_num_t PIN_USB_DM   = GPIO_NUM_19;
static const gpio_num_t PIN_USB_DP   = GPIO_NUM_20;

// --- Logging status NeoPixel ---
static const gpio_num_t PIN_NEOPIXEL = GPIO_NUM_21;

// --- RTC (RX8900CE) ---
static const gpio_num_t PIN_RTC_FOUT = GPIO_NUM_33;
static const gpio_num_t PIN_RTC_INT  = GPIO_NUM_34;

// --- IMU (LSM6DSV320X) interrupts ---
static const gpio_num_t PIN_IMU_INT1 = GPIO_NUM_39;
static const gpio_num_t PIN_IMU_INT2 = GPIO_NUM_40;

// --- I2C bus for RTC + IMU + gauge, etc. ---
static const gpio_num_t PIN_I2C_SDA  = GPIO_NUM_41;
static const gpio_num_t PIN_I2C_SCL  = GPIO_NUM_42;
