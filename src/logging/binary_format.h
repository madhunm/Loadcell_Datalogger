/**
 * @file binary_format.h
 * @brief Binary log file format definitions
 */

#ifndef BINARY_FORMAT_H
#define BINARY_FORMAT_H

#include <Arduino.h>

/** @brief Magic number to identify loadcell log files */
#define LOG_MAGIC 0x4C434C47 // "LCLG"

/** @brief Current format version */
#define LOG_VERSION 1

/**
 * @brief Log file header (64 bytes)
 * Written once at the start of each log file
 */
struct __attribute__((packed)) LogFileHeader {
  uint32_t magic;              ///< Magic number (0x4C434C47 "LCLG")
  uint16_t version;            ///< Format version
  uint16_t header_size;        ///< Size of this header (64 bytes)
  uint32_t sample_rate_hz;     ///< ADC sample rate in Hz
  uint32_t imu_rate_hz;        ///< IMU sample rate in Hz
  uint64_t start_timestamp_us; ///< Start time in microseconds since epoch
  char loadcell_id[32];        ///< Active loadcell ID
  uint8_t reserved[8];         ///< Reserved for future use

  /** @brief Initialize header with default values */
  LogFileHeader() {
    magic = LOG_MAGIC;
    version = LOG_VERSION;
    header_size = sizeof(LogFileHeader);
    sample_rate_hz = 64000;
    imu_rate_hz = 1000;
    start_timestamp_us = 0;
    memset(loadcell_id, 0, sizeof(loadcell_id));
    memset(reserved, 0, sizeof(reserved));
  }
};

/**
 * @brief Loadcell sample record (8 bytes)
 */
struct __attribute__((packed)) LoadcellSample {
  uint32_t timestamp_offset_us; ///< Microseconds since start_timestamp_us
  int32_t raw_adc; ///< Raw 24-bit ADC value (sign-extended to 32-bit)
};

// Note: IMUSample is defined in lsm6dsv_driver.h to avoid circular dependencies

// Verify struct sizes at compile time
static_assert(sizeof(LogFileHeader) == 64, "LogFileHeader must be 64 bytes");
static_assert(sizeof(LoadcellSample) == 8, "LoadcellSample must be 8 bytes");

#endif // BINARY_FORMAT_H