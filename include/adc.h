#pragma once

#include <Arduino.h>
#include <SPI.h>

// MAX11270 ADC driver for ratiometric load cell
// Wiring (ESP32 pins, from your pin map):
//   IO12 -> MISO (DOUT)
//   IO13 -> MOSI (DIN)
//   IO14 -> SYNC
//   IO15 -> RSTB
//   IO16 -> RDYB (data ready, active LOW)
//   IO17 -> CSB
//   IO18 -> SCLK

// Pin definitions
static const int ADC_MISO_PIN = 12;
static const int ADC_MOSI_PIN = 13;
static const int ADC_SYNC_PIN = 14;
static const int ADC_RSTB_PIN = 15;
static const int ADC_RDYB_PIN = 16;
static const int ADC_CS_PIN   = 17;
static const int ADC_SCK_PIN  = 18;

// SPI configuration
static const uint32_t ADC_SPI_CLOCK_HZ   = 4000000UL;  // 4 MHz, inside MAX11270 spec
static const uint8_t  ADC_SPI_MODE       = SPI_MODE0;  // CPOL=0, CPHA=0
static const uint8_t  ADC_SPI_BIT_ORDER  = MSBFIRST;

// MAX11270 register addresses (RS[4:0])
enum AdcRegister : uint8_t {
    ADC_REG_STAT     = 0x00,
    ADC_REG_CTRL1    = 0x01,
    ADC_REG_CTRL2    = 0x02,
    ADC_REG_CTRL3    = 0x03,
    ADC_REG_CTRL4    = 0x04,
    ADC_REG_CTRL5    = 0x05,
    ADC_REG_DATA     = 0x06,
    ADC_REG_SOC_SPI  = 0x07,
    ADC_REG_SGC_SPI  = 0x08,
    ADC_REG_SCOC_SPI = 0x09,
    ADC_REG_SCGC_SPI = 0x0A,
    ADC_REG_RAM      = 0x0C,
    ADC_REG_SYNC_SPI = 0x0D,
    ADC_REG_SOC_ADC  = 0x15,
    ADC_REG_SGC_ADC  = 0x16,
    ADC_REG_SCOC_ADC = 0x17,
    ADC_REG_SCGC_ADC = 0x18
};

// NOTE on PGA gain:
// pgaGainCode is PGAG[2:0] from CTRL2:
//
//  pgaGainCode  Analog PGA gain
//  ------------ ----------------
//      0        x1
//      1        x2
//      2        x4
//      3        x8
//      4        x16
//      5        x32
//      6        x64
//      7        x128
//
// Until you know your load cell sensitivity, you can experiment with this
// from main.cpp without touching the driver.

// Configure GPIOs, SPI, reset the MAX11270 and set CTRL1/2.
// pgaGainCode: 0..7 (PGAG bits) as above.
bool adcInit(uint8_t pgaGainCode);

// Start continuous conversions at the given RATE[3:0] code.
// Default is 0x0F = 64 ksps (continuous mode).
bool adcStartContinuous(uint8_t rateCode = 0x0F);

// Return true if RDYB is asserted (active low, meaning data ready).
bool adcIsDataReady();

// Read one 24-bit conversion result from the DATA register,
// sign-extend it to a 32-bit signed integer.
// Returns true on success, false if SPI fails for some reason.
bool adcReadSample(int32_t &code);

// Optional helpers if you want to tweak config later:
bool adcWriteRegister(uint8_t reg, uint8_t value);
bool adcReadRegister(uint8_t reg, uint8_t &value);

// Convert raw ADC code to a normalized float in +/-FS range.
// This assumes bipolar, 24-bit two's complement data.
inline float adcCodeToNormalized(int32_t code)
{
    // For 24-bit two's complement, full-scale range is [-2^23 .. 2^23 - 1].
    const float denom = 8388608.0f; // 2^23
    return static_cast<float>(code) / denom;
}
