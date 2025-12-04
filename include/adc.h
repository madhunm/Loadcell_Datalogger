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
static const int ADC_CS_PIN = 17;
static const int ADC_SCK_PIN = 18;

// SPI configuration
static const uint32_t ADC_SPI_CLOCK_HZ = 4000000UL; // 4 MHz
static const uint8_t ADC_SPI_MODE = SPI_MODE0;      // CPOL=0, CPHA=0
static const uint8_t ADC_SPI_BIT_ORDER = MSBFIRST;

// MAX11270 register addresses (RS[4:0])
enum AdcRegister : uint8_t
{
    ADC_REG_STAT = 0x00,
    ADC_REG_CTRL1 = 0x01,
    ADC_REG_CTRL2 = 0x02,
    ADC_REG_CTRL3 = 0x03,
    ADC_REG_CTRL4 = 0x04,
    ADC_REG_CTRL5 = 0x05,
    ADC_REG_DATA = 0x06,
    ADC_REG_SOC_SPI = 0x07,
    ADC_REG_SGC_SPI = 0x08,
    ADC_REG_SCOC_SPI = 0x09,
    ADC_REG_SCGC_SPI = 0x0A,
    ADC_REG_RAM = 0x0C,
    ADC_REG_SYNC_SPI = 0x0D,
    ADC_REG_SOC_ADC = 0x15,
    ADC_REG_SGC_ADC = 0x16,
    ADC_REG_SCOC_ADC = 0x17,
    ADC_REG_SCGC_ADC = 0x18
};

// Analog PGA gain options (PGAG2:0 in CTRL2).
enum AdcPgaGain : uint8_t
{
    ADC_PGA_GAIN_1 = 0,  // x1
    ADC_PGA_GAIN_2 = 1,  // x2
    ADC_PGA_GAIN_4 = 2,  // x4
    ADC_PGA_GAIN_8 = 3,  // x8
    ADC_PGA_GAIN_16 = 4, // x16
    ADC_PGA_GAIN_32 = 5, // x32
    ADC_PGA_GAIN_64 = 6, // x64
    ADC_PGA_GAIN_128 = 7 // x128
};

inline uint16_t adcPgaGainFactor(AdcPgaGain gain)
{
    return static_cast<uint16_t>(1U << static_cast<uint8_t>(gain));
}

// One ADC sample in the ring buffer
struct AdcSample
{
    uint32_t index; // monotonically increasing sample index
    int32_t code;   // raw 24-bit sign-extended code
};

// Configure GPIOs, SPI, reset the MAX11270, set CTRL1/2, and run self-cal.
// pgaGain selects the analog PGA gain (x1..x128).
bool adcInit(AdcPgaGain pgaGain);

// Start continuous conversions at the given RATE[3:0] code.
// Default is 0x0F = 64 ksps (continuous mode).
bool adcStartContinuous(uint8_t rateCode = 0x0F);

// Perform self-calibration (offset + gain).
bool adcSelfCalibrate(uint8_t rateCode = 0x0F, uint32_t timeoutMs = 500);

// Return true if RDYB is asserted (active low).
bool adcIsDataReady();

// Read one 24-bit conversion result (sign-extended to 32-bit).
bool adcReadSample(int32_t &code);

// Low-level register access (optional for debug).
bool adcWriteRegister(uint8_t reg, uint8_t value);
bool adcReadRegister(uint8_t reg, uint8_t &value);

// ---- Ring buffer API ----

// Start a high-priority sampling task pinned to a given core.
// This task polls RDYB and pushes samples into a ring buffer.
// Returns true if task was created successfully, false on failure.
bool adcStartSamplingTask(UBaseType_t coreId = 0);

// Pop the next sample from the ring buffer.
// Returns true if a sample was available.
bool adcGetNextSample(AdcSample &sample);

// Approx number of samples currently buffered.
size_t adcGetBufferedSampleCount();

// Number of times the ring buffer overflowed (samples dropped).
size_t adcGetOverflowCount();

// Return the current sample counter (monotonically increasing).
// Used to align IMU samples to ADC sample index.
uint32_t adcGetSampleCounter();

// Convert raw ADC code to normalized float in +/-FS range.
inline float adcCodeToNormalized(int32_t code)
{
    const float denom = 8388608.0f; // 2^23
    return static_cast<float>(code) / denom;
}
