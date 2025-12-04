#pragma once

#include <Arduino.h>

#include "rtc.h"
#include "adc.h"
#include "imu.h"

// Logging session configuration passed in from main code.
// Keep this simple and explicit so main.cpp clearly owns the config.
struct LoggerConfig
{
    uint32_t   adcSampleRate;   // e.g. 64000
    AdcPgaGain adcPgaGain;      // ADC_PGA_GAIN_4 etc.

    uint16_t   imuAccelRange;   // e.g. 16 for ±16 g
    uint16_t   imuGyroRange;    // e.g. 2000 for 2000 dps
    uint32_t   imuOdr;          // e.g. 960 Hz
};

// Internal logger state (for introspection if needed).
enum LoggerState
{
    LOGGER_IDLE = 0,          // No session open
    LOGGER_SESSION_OPEN,      // Binary log file open, logging active
    LOGGER_CONVERTING         // Converting last session to CSV
};

// On-disk header for ADC binary log file (.adc).
// All fields are little-endian. This is written once at the start of the file.
#pragma pack(push, 1)
struct AdcLogFileHeader
{
    char     magic[8];          // "ADCLOGV1" + '\0'
    uint16_t headerSize;        // sizeof(AdcLogFileHeader)
    uint16_t version;           // 0x0001

    // ADC configuration
    uint32_t adcSampleRate;     // e.g. 64000
    uint8_t  adcPgaGainCode;    // 0..7 -> x1..x128 (AdcPgaGain)
    uint8_t  reserved1[3];      // padding / future use

    // RTC time when logging started
    uint16_t rtcYear;
    uint8_t  rtcMonth;
    uint8_t  rtcDay;
    uint8_t  rtcHour;
    uint8_t  rtcMinute;
    uint8_t  rtcSecond;
    uint8_t  rtcWeekday;        // 0..6

    // Sample timebase anchor: ADC sample index at log start
    uint32_t adcIndexAtStart;

    // Reserved for future expansion (e.g. calibration constants)
    uint8_t  reserved2[16];
};
#pragma pack(pop)

// On-disk header for IMU binary log file (.imu).
// All fields are little-endian. This is written once at the start of the file.
#pragma pack(push, 1)
struct ImuLogFileHeader
{
    char     magic[8];          // "IMULOGV1" + '\0'
    uint16_t headerSize;        // sizeof(ImuLogFileHeader)
    uint16_t version;           // 0x0001

    // IMU configuration
    uint16_t imuAccelRange;     // e.g. 16 (±16 g)
    uint16_t imuGyroRange;       // e.g. 2000 (2000 dps)
    uint32_t imuOdr;            // e.g. 960 Hz
    uint8_t  reserved1[4];      // padding / future use

    // RTC time when logging started
    uint16_t rtcYear;
    uint8_t  rtcMonth;
    uint8_t  rtcDay;
    uint8_t  rtcHour;
    uint8_t  rtcMinute;
    uint8_t  rtcSecond;
    uint8_t  rtcWeekday;        // 0..6

    // Sample timebase anchor: ADC sample index at log start (for correlation)
    uint32_t adcIndexAtStart;

    // Reserved for future expansion
    uint8_t  reserved2[16];
};
#pragma pack(pop)

// Binary record structures for on-disk storage
// All fields are little-endian.

// ADC sample record (written to .adc file)
#pragma pack(push, 1)
struct AdcLogRecord
{
    uint32_t index;     // ADC sample index (monotonically increasing)
    int32_t  code;      // Raw 24-bit sign-extended ADC code
};
#pragma pack(pop)

// IMU sample record (written to .imu file)
#pragma pack(push, 1)
struct ImuLogRecord
{
    uint32_t index;         // IMU sample index (monotonically increasing)
    uint32_t adcSampleIndex; // ADC sample index at time of IMU read (for alignment)
    float    ax, ay, az;    // Accelerometer (g)
    float    gx, gy, gz;    // Gyroscope (dps)
};
#pragma pack(pop)

// ---- Public logger API ----

// Initialise/prepare logger module (optional for now, but available).
// Currently does nothing but gives us a hook for future work.
void loggerInit();

// Start a new logging session.
// - Uses sdCardGetFs() and rtcGetDateTime() internally.
// - Opens two binary files: baseName_ADC.bin and baseName_IMU.bin
// - Writes headers to both files.
// - Stores the base name (stem) for later CSV conversion.
// Returns true on success, false on any error (SD not mounted, RTC read fail, etc).
bool loggerStartSession(const LoggerConfig &config);

// Returns true if a binary log session is currently open.
bool loggerIsSessionOpen();

// Get current logger state (IDLE / SESSION_OPEN / CONVERTING).
LoggerState loggerGetState();

// Called regularly from STATE_LOGGING in main.cpp.
// Drains ADC and IMU ring buffers and writes records to respective binary files.
// Writes are buffered for efficiency; periodic flushing is handled internally.
void loggerTick();

// Stop the current session and flush any pending data.
// Stage 1: this just closes the binary file if open.
// Stage 2/3: we’ll extend it to flush ring buffers before closing.
bool loggerStopSessionAndFlush();

// Begin converting the most recent session's binary file to CSV.
// Stage 1: stub that just returns false (not implemented yet).
// Stage 3: will do real binary->CSV conversion.
bool loggerConvertLastSessionToCsv();

// Returns true if we have a remembered "last session" (base name / filenames).
bool loggerHasLastSession();

// Convenience accessors: base name and filenames for current/last session.
// If no session/last-session is known, these return empty strings.
String loggerGetCurrentBaseName();
String loggerGetCurrentAdcFilename();
String loggerGetCurrentImuFilename();
String loggerGetCurrentCsvFilename();
