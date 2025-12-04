#include "logger.h"

#include "sdcard.h"
#include "rtc.h"
#include "adc.h"
#include "imu.h"
#include "neopixel.h"

#include "FS.h"
#include <cstring>
#include "esp_crc.h"  // For CRC32 calculation
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Debug flags
#ifndef LOGGER_DEBUG
#define LOGGER_DEBUG 0
#endif

#define LOG_DEBUG(fmt, ...) do { if (LOGGER_DEBUG) Serial.printf(fmt, ##__VA_ARGS__); } while(0)
#define LOG_ERROR(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)  // Always log errors

// Internal logger state
static LoggerState   s_loggerState        = LOGGER_IDLE;
static bool          s_sessionOpen        = false;

// Filesystem and file handles
static fs::FS       *s_fs                 = nullptr;
static File          s_adcFile;                     // ADC binary log file
static File          s_imuFile;                     // IMU binary log file

// Session metadata
static LoggerConfig  s_currentConfig      = {};
static RtcDateTime   s_startRtc           = {};
static uint32_t      s_adcIndexAtStart    = 0;

// Filenames
static String        s_baseName;                    // e.g. "20251204_153012"
static String        s_adcFilename;                 // e.g. "/log/20251204_153012_ADC.bin"
static String        s_imuFilename;                 // e.g. "/log/20251204_153012_IMU.bin"
static String        s_csvFilename;                 // e.g. "/log/20251204_153012.csv"

// Track if we have a "last session" suitable for conversion
static bool          s_hasLastSession     = false;

// CSV conversion task state
static TaskHandle_t  s_csvTaskHandle      = nullptr;
static bool          s_csvConversionInProgress = false;
static bool          s_csvConversionResult = false;

// Write buffering for efficiency
static const size_t  WRITE_BUFFER_SIZE    = 8192;   // 8 KB buffer per file
static uint8_t       s_adcWriteBuffer[WRITE_BUFFER_SIZE];
static uint8_t       s_imuWriteBuffer[WRITE_BUFFER_SIZE];
static size_t        s_adcBufferPos        = 0;
static size_t        s_imuBufferPos        = 0;

// Write failure tracking and retry logic
static const uint32_t MAX_CONSECUTIVE_FAILURES = 10;  // Stop session after this many consecutive failures
static const uint32_t MAX_RETRY_ATTEMPTS = 4;          // Retry up to 4 times with exponential backoff
static const uint32_t INITIAL_RETRY_DELAY_MS = 1;      // Initial retry delay: 1ms

// Write statistics
static LoggerWriteStats s_writeStats = {0};

// CRC32 calculation for data integrity
static uint32_t s_adcCrc32 = 0;  // Running CRC32 for ADC data
static uint32_t s_imuCrc32 = 0;  // Running CRC32 for IMU data

// ---- Internal helpers ----

// Format a base name like "YYYYMMDD_HHMMSS" from an RtcDateTime.
static String makeBaseNameFromRtc(const RtcDateTime &dt)
{
    char buf[32];
    // YYYYMMDD_HHMMSS
    snprintf(buf,
             sizeof(buf),
             "%04u%02u%02u_%02u%02u%02u",
             static_cast<unsigned>(dt.year),
             static_cast<unsigned>(dt.month),
             static_cast<unsigned>(dt.day),
             static_cast<unsigned>(dt.hour),
             static_cast<unsigned>(dt.minute),
             static_cast<unsigned>(dt.second));
    return String(buf);
}

// Fill an AdcLogFileHeader structure from the current config and RTC.
static void fillAdcLogFileHeader(AdcLogFileHeader &hdr,
                                const LoggerConfig &config,
                                const RtcDateTime &startRtc,
                                uint32_t adcIndexAtStart)
{
    memset(&hdr, 0, sizeof(hdr));

    // Magic + version
    const char magicStr[8] = { 'A', 'D', 'C', 'L', 'O', 'G', 'V', '1' };
    memcpy(hdr.magic, magicStr, sizeof(hdr.magic));

    hdr.headerSize = sizeof(AdcLogFileHeader);
    hdr.version    = 0x0001;

    // ADC config
    hdr.adcSampleRate  = config.adcSampleRate;
    hdr.adcPgaGainCode = static_cast<uint8_t>(config.adcPgaGain);
    hdr.reserved1[0]   = 0;
    hdr.reserved1[1]   = 0;
    hdr.reserved1[2]   = 0;

    // RTC
    hdr.rtcYear    = startRtc.year;
    hdr.rtcMonth   = startRtc.month;
    hdr.rtcDay     = startRtc.day;
    hdr.rtcHour    = startRtc.hour;
    hdr.rtcMinute  = startRtc.minute;
    hdr.rtcSecond  = startRtc.second;
    hdr.rtcWeekday = startRtc.weekday;

    // Sample timebase anchor
    hdr.adcIndexAtStart = adcIndexAtStart;

    // Initialize CRC32 to 0 (will be updated at session end)
    hdr.dataCrc32 = 0;

    // reserved2[] already zeroed by memset()
}

// Fill an ImuLogFileHeader structure from the current config and RTC.
static void fillImuLogFileHeader(ImuLogFileHeader &hdr,
                                 const LoggerConfig &config,
                                 const RtcDateTime &startRtc,
                                 uint32_t adcIndexAtStart)
{
    memset(&hdr, 0, sizeof(hdr));

    // Magic + version
    const char magicStr[8] = { 'I', 'M', 'U', 'L', 'O', 'G', 'V', '1' };
    memcpy(hdr.magic, magicStr, sizeof(hdr.magic));

    hdr.headerSize = sizeof(ImuLogFileHeader);
    hdr.version    = 0x0001;

    // IMU config
    hdr.imuAccelRange  = config.imuAccelRange;
    hdr.imuGyroRange   = config.imuGyroRange;
    hdr.imuOdr         = config.imuOdr;
    hdr.reserved1[0]   = 0;
    hdr.reserved1[1]   = 0;
    hdr.reserved1[2]   = 0;
    hdr.reserved1[3]   = 0;

    // RTC
    hdr.rtcYear    = startRtc.year;
    hdr.rtcMonth   = startRtc.month;
    hdr.rtcDay     = startRtc.day;
    hdr.rtcHour    = startRtc.hour;
    hdr.rtcMinute  = startRtc.minute;
    hdr.rtcSecond  = startRtc.second;
    hdr.rtcWeekday = startRtc.weekday;

    // Sample timebase anchor (for correlation with ADC samples)
    hdr.adcIndexAtStart = adcIndexAtStart;

    // Initialize CRC32 to 0 (will be updated at session end)
    hdr.dataCrc32 = 0;

    // reserved2[] already zeroed by memset()
}

// Flush ADC write buffer to file with retry logic
// Returns true on success, false on failure
static bool flushAdcBuffer()
{
    if (s_adcBufferPos == 0 || !s_adcFile)
    {
        return true;  // Nothing to flush or file not open
    }

    // Retry with exponential backoff
    uint32_t delayMs = INITIAL_RETRY_DELAY_MS;
    for (uint32_t attempt = 0; attempt < MAX_RETRY_ATTEMPTS; attempt++)
    {
        size_t written = s_adcFile.write(s_adcWriteBuffer, s_adcBufferPos);
        
        if (written == s_adcBufferPos)
        {
            // Success - update CRC32 and reset failure counter
            s_adcCrc32 = esp_crc32_le(s_adcCrc32, s_adcWriteBuffer, s_adcBufferPos);
            s_writeStats.adcConsecutiveFailures = 0;
            s_adcBufferPos = 0;
            return true;
        }
        
        // Write failed - retry with exponential backoff
        s_writeStats.adcWriteFailures++;
        s_writeStats.adcConsecutiveFailures++;
        
        if (attempt < MAX_RETRY_ATTEMPTS - 1)
        {
            delay(delayMs);
            delayMs *= 2;  // Exponential backoff: 1ms, 2ms, 4ms, 8ms
        }
    }
    
    // All retries failed
    Serial.printf("[LOGGER] ERROR: Failed to flush ADC buffer after %d attempts\n", MAX_RETRY_ATTEMPTS);
    return false;
}

// Flush IMU write buffer to file with retry logic
// Returns true on success, false on failure
static bool flushImuBuffer()
{
    if (s_imuBufferPos == 0 || !s_imuFile)
    {
        return true;  // Nothing to flush or file not open
    }

    // Retry with exponential backoff
    uint32_t delayMs = INITIAL_RETRY_DELAY_MS;
    for (uint32_t attempt = 0; attempt < MAX_RETRY_ATTEMPTS; attempt++)
    {
        size_t written = s_imuFile.write(s_imuWriteBuffer, s_imuBufferPos);
        
        if (written == s_imuBufferPos)
        {
            // Success - update CRC32 and reset failure counter
            s_imuCrc32 = esp_crc32_le(s_imuCrc32, s_imuWriteBuffer, s_imuBufferPos);
            s_writeStats.imuConsecutiveFailures = 0;
            s_imuBufferPos = 0;
            return true;
        }
        
        // Write failed - retry with exponential backoff
        s_writeStats.imuWriteFailures++;
        s_writeStats.imuConsecutiveFailures++;
        
        if (attempt < MAX_RETRY_ATTEMPTS - 1)
        {
            delay(delayMs);
            delayMs *= 2;  // Exponential backoff: 1ms, 2ms, 4ms, 8ms
        }
    }
    
    // All retries failed
    Serial.printf("[LOGGER] ERROR: Failed to flush IMU buffer after %d attempts\n", MAX_RETRY_ATTEMPTS);
    return false;
}

// Write ADC record to buffer (flushes buffer if full)
// Returns true on success, false on failure
static bool writeAdcRecord(const AdcLogRecord &record)
{
    const size_t recordSize = sizeof(AdcLogRecord);
    
    // Check if record fits in remaining buffer space
    if (s_adcBufferPos + recordSize > WRITE_BUFFER_SIZE)
    {
        // Buffer full - flush it first
        if (!flushAdcBuffer())
        {
            return false;  // Flush failed
        }
    }
    
    // Copy record to buffer
    memcpy(&s_adcWriteBuffer[s_adcBufferPos], &record, recordSize);
    s_adcBufferPos += recordSize;
    s_writeStats.adcRecordsWritten++;
    
    return true;
}

// Write IMU record to buffer (flushes buffer if full)
// Returns true on success, false on failure
static bool writeImuRecord(const ImuLogRecord &record)
{
    const size_t recordSize = sizeof(ImuLogRecord);
    
    // Check if record fits in remaining buffer space
    if (s_imuBufferPos + recordSize > WRITE_BUFFER_SIZE)
    {
        // Buffer full - flush it first
        if (!flushImuBuffer())
        {
            return false;  // Flush failed
        }
    }
    
    // Copy record to buffer
    memcpy(&s_imuWriteBuffer[s_imuBufferPos], &record, recordSize);
    s_imuBufferPos += recordSize;
    s_writeStats.imuRecordsWritten++;
    
    return true;
}

// ---- Public API ----

void loggerInit()
{
    // Nothing to do yet, but this gives us a hook if we ever need to
    // initialise internal structures.
    s_loggerState     = LOGGER_IDLE;
    s_sessionOpen     = false;
    s_fs              = nullptr;
    s_hasLastSession  = false;
    s_baseName        = "";
    s_adcFilename     = "";
    s_imuFilename     = "";
    s_csvFilename     = "";
    s_adcBufferPos    = 0;
    s_imuBufferPos    = 0;
    
    // Reset write statistics
    memset(&s_writeStats, 0, sizeof(s_writeStats));
    
    // Initialize CRC32
    s_adcCrc32 = 0;
    s_imuCrc32 = 0;
}

bool loggerStartSession(const LoggerConfig &config)
{
    // Ensure SD card is mounted
    if (!sdCardIsMounted())
    {
        LOG_ERROR("[LOGGER] Cannot start session: SD card is not mounted.\n");
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_SD);
        return false;
    }

    // Check available SD card space
    // Estimate space needed: assume 1 minute of logging as minimum
    // ADC: 64k samples/sec * 8 bytes/sample = 512 KB/sec
    // IMU: 960 samples/sec * 28 bytes/sample = ~27 KB/sec
    // Total: ~539 KB/sec = ~32 MB/min
    // Require at least 10 MB free (safety margin) or 1 minute worth, whichever is larger
    uint64_t freeSpace = sdCardGetFreeSpace();
    uint64_t minRequiredSpace = 10ULL * 1024 * 1024;  // 10 MB minimum
    uint64_t oneMinuteSpace = 32ULL * 1024 * 1024;     // ~32 MB for 1 minute
    
    if (freeSpace < minRequiredSpace && freeSpace < oneMinuteSpace)
    {
        LOG_ERROR("[LOGGER] ERROR: Insufficient SD card space. Free: %llu bytes, Required: %llu bytes\n",
                 freeSpace, minRequiredSpace > oneMinuteSpace ? minRequiredSpace : oneMinuteSpace);
        neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_LOW_SPACE);
        return false;
    }
    
    LOG_DEBUG("[LOGGER] SD card free space: %llu MB\n", freeSpace / (1024ULL * 1024ULL));

    // Reset write statistics and CRC32
    memset(&s_writeStats, 0, sizeof(s_writeStats));
    s_adcCrc32 = 0;
    s_imuCrc32 = 0;

    // Get filesystem
    s_fs = &sdCardGetFs();

    // Read current RTC time
    RtcDateTime nowRtc;
    if (!rtcGetDateTime(nowRtc))
    {
        LOG_ERROR("[LOGGER] Cannot read RTC date/time; aborting session start.\n");
        return false;
    }

    // Determine ADC sample index at start
    uint32_t adcIndexNow = adcGetSampleCounter();

    // Generate base name from RTC
    String baseName = makeBaseNameFromRtc(nowRtc);
    if (baseName.length() == 0)
    {
        LOG_ERROR("[LOGGER] Failed to generate base name from RTC.\n");
        return false;
    }

    // Build filenames using stack buffers (avoid String concatenation)
    char adcFilename[64], imuFilename[64], csvFilename[64];
    snprintf(adcFilename, sizeof(adcFilename), "/log/%s_ADC.bin", baseName.c_str());
    snprintf(imuFilename, sizeof(imuFilename), "/log/%s_IMU.bin", baseName.c_str());
    snprintf(csvFilename, sizeof(csvFilename), "/log/%s.csv", baseName.c_str());

    LOG_DEBUG("[LOGGER] Starting session with base name: %s\n", baseName.c_str());
    LOG_DEBUG("[LOGGER] ADC log file: %s\n", adcFilename);
    LOG_DEBUG("[LOGGER] IMU log file: %s\n", imuFilename);

    // Open ADC binary file for write (truncate if exists)
    File adcFile = s_fs->open(adcFilename, FILE_WRITE);
    if (!adcFile)
    {
        LOG_ERROR("[LOGGER] Failed to open ADC binary log file for writing.\n");
        return false;
    }

    // Open IMU binary file for write (truncate if exists)
    File imuFile = s_fs->open(imuFilename, FILE_WRITE);
    if (!imuFile)
    {
        LOG_ERROR("[LOGGER] Failed to open IMU binary log file for writing.\n");
        adcFile.close();
        return false;
    }

    // Fill headers and write them
    AdcLogFileHeader adcHdr;
    fillAdcLogFileHeader(adcHdr, config, nowRtc, adcIndexNow);

    ImuLogFileHeader imuHdr;
    fillImuLogFileHeader(imuHdr, config, nowRtc, adcIndexNow);

    size_t adcWritten = adcFile.write(reinterpret_cast<const uint8_t *>(&adcHdr),
                                      sizeof(AdcLogFileHeader));
    size_t imuWritten = imuFile.write(reinterpret_cast<const uint8_t *>(&imuHdr),
                                      sizeof(ImuLogFileHeader));

    if (adcWritten != sizeof(AdcLogFileHeader))
    {
        LOG_ERROR("[LOGGER] Failed to write full ADC log header; aborting.\n");
        adcFile.close();
        imuFile.close();
        return false;
    }

    if (imuWritten != sizeof(ImuLogFileHeader))
    {
        LOG_ERROR("[LOGGER] Failed to write full IMU log header; aborting.\n");
        adcFile.close();
        imuFile.close();
        return false;
    }

    // Commit to internal state
    s_adcFile          = adcFile;  // File object copy
    s_imuFile          = imuFile;  // File object copy
    s_currentConfig    = config;
    s_startRtc         = nowRtc;
    s_adcIndexAtStart  = adcIndexNow;

    // Store filenames as String (needed for compatibility with existing code)
    s_baseName         = baseName;
    s_adcFilename      = String(adcFilename);
    s_imuFilename      = String(imuFilename);
    s_csvFilename      = String(csvFilename);

    s_adcBufferPos     = 0;
    s_imuBufferPos     = 0;

    s_sessionOpen      = true;
    s_hasLastSession   = true;
    s_loggerState      = LOGGER_SESSION_OPEN;

    LOG_DEBUG("[LOGGER] Session started and headers written to both files.\n");

    return true;
}

bool loggerIsSessionOpen()
{
    return s_sessionOpen;
}

LoggerState loggerGetState()
{
    return s_loggerState;
}

void loggerTick()
{
    if (!s_sessionOpen)
    {
        return;
    }

    // Check for SD card removal using card detect pin
    // This provides early detection before write failures occur
    if (!sdCardCheckPresent())
    {
        Serial.println("[LOGGER] ERROR: SD card removed during logging!");
        // Stop logging session gracefully
        loggerStopSessionAndFlush();
        s_loggerState = LOGGER_IDLE;
        return;
    }

    // Limit samples processed per tick to prevent blocking
    // CRITICAL: These limits must allow processing faster than sample arrival rate
    // to maintain 64 ksps sampling without buffer overflow.
    //
    // At 64 ksps: 64,000 samples/sec = 640 samples per 10ms (main loop tick rate)
    // At 960 Hz IMU: 960 samples/sec = ~10 samples per 10ms
    //
    // Processing time per sample: ~1-2µs (just copying to buffer)
    // So processing 2000 samples takes ~2-4ms, well within 10ms loop time
    //
    // Limits chosen:
    // - MAX_ADC_SAMPLES_PER_TICK = 2000: Processes ~31ms worth of data max
    //   This ensures we can drain buffers faster than they fill, maintaining 64 ksps
    // - MAX_IMU_SAMPLES_PER_TICK = 200: Processes ~208ms worth of data max
    //   More than enough headroom for 960 Hz rate
    //
    // These limits ensure:
    // - 64 ksps rate is maintained (processing > arrival rate)
    // - Main loop remains responsive (max ~4ms blocking)
    // - Web server, NeoPixel, buttons still work
    // - Watchdog timer safe
    const size_t MAX_ADC_SAMPLES_PER_TICK = 2000;  // ~31ms of data at 64 ksps
    const size_t MAX_IMU_SAMPLES_PER_TICK = 200;   // ~208ms of data at 960 Hz

    // Drain ADC ring buffer and write records (with limit)
    AdcSample adcSample;
    size_t adcSamplesProcessed = 0;
    while (adcGetNextSample(adcSample) && adcSamplesProcessed < MAX_ADC_SAMPLES_PER_TICK)
    {
        AdcLogRecord record;
        record.index = adcSample.index;
        record.code  = adcSample.code;
        
        if (!writeAdcRecord(record))
        {
            // Check if SD card was removed
            if (!sdCardCheckPresent())
            {
                LOG_ERROR("[LOGGER] SD card removed - stopping session\n");
                loggerStopSessionAndFlush();
                s_loggerState = LOGGER_IDLE;
                neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_SD);
                return;
            }
            
            // Check for too many consecutive failures
            if (s_writeStats.adcConsecutiveFailures >= MAX_CONSECUTIVE_FAILURES)
            {
                LOG_ERROR("[LOGGER] ERROR: Too many consecutive ADC write failures (%u). Stopping session.\n",
                         s_writeStats.adcConsecutiveFailures);
                loggerStopSessionAndFlush();
                s_loggerState = LOGGER_IDLE;
                neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_WRITE_FAILURE);
                return;
            }
        }
        adcSamplesProcessed++;
    }

    // Drain IMU ring buffer and write records (with limit)
    ImuSample imuSample;
    size_t imuSamplesProcessed = 0;
    while (imuGetNextSample(imuSample) && imuSamplesProcessed < MAX_IMU_SAMPLES_PER_TICK)
    {
        ImuLogRecord record;
        record.index         = imuSample.index;
        record.adcSampleIndex = imuSample.adcSampleIndex;
        record.ax            = imuSample.ax;
        record.ay            = imuSample.ay;
        record.az            = imuSample.az;
        record.gx            = imuSample.gx;
        record.gy            = imuSample.gy;
        record.gz            = imuSample.gz;
        
        if (!writeImuRecord(record))
        {
            // Check if SD card was removed
            if (!sdCardCheckPresent())
            {
                LOG_ERROR("[LOGGER] SD card removed - stopping session\n");
                loggerStopSessionAndFlush();
                s_loggerState = LOGGER_IDLE;
                neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_SD);
                return;
            }
            
            // Check for too many consecutive failures
            if (s_writeStats.imuConsecutiveFailures >= MAX_CONSECUTIVE_FAILURES)
            {
                LOG_ERROR("[LOGGER] ERROR: Too many consecutive IMU write failures (%u). Stopping session.\n",
                         s_writeStats.imuConsecutiveFailures);
                loggerStopSessionAndFlush();
                s_loggerState = LOGGER_IDLE;
                neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_WRITE_FAILURE);
                return;
            }
        }
        imuSamplesProcessed++;
    }

    // Periodic flush (every ~100ms worth of data or when buffers are getting full)
    // For 64k samples/sec ADC, 100ms = 6400 samples = ~51KB, so flush frequently
    static uint32_t lastFlushMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastFlushMs > 100)  // Flush every 100ms
    {
        // Flush buffers and check for failures
        bool adcFlushOk = flushAdcBuffer();
        bool imuFlushOk = flushImuBuffer();
        
        // Check for consecutive flush failures
        if (!adcFlushOk && s_writeStats.adcConsecutiveFailures >= MAX_CONSECUTIVE_FAILURES)
        {
            LOG_ERROR("[LOGGER] ERROR: Too many consecutive ADC flush failures. Stopping session.\n");
            loggerStopSessionAndFlush();
            s_loggerState = LOGGER_IDLE;
            neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_WRITE_FAILURE);
            return;
        }
        
        if (!imuFlushOk && s_writeStats.imuConsecutiveFailures >= MAX_CONSECUTIVE_FAILURES)
        {
            LOG_ERROR("[LOGGER] ERROR: Too many consecutive IMU flush failures. Stopping session.\n");
            loggerStopSessionAndFlush();
            s_loggerState = LOGGER_IDLE;
            neopixelSetPattern(NEOPIXEL_PATTERN_ERROR_WRITE_FAILURE);
            return;
        }
        
        lastFlushMs = nowMs;
    }
}

bool loggerStopSessionAndFlush()
{
    if (!s_sessionOpen)
    {
        Serial.println("[LOGGER] No open session to stop.");
        return false;
    }

    LOG_DEBUG("[LOGGER] Stopping session: draining buffers, flushing, and closing files.\n");

    // Final drain of ring buffers before closing
    AdcSample adcSample;
    while (adcGetNextSample(adcSample))
    {
        AdcLogRecord record;
        record.index = adcSample.index;
        record.code  = adcSample.code;
        writeAdcRecord(record);
    }

    ImuSample imuSample;
    while (imuGetNextSample(imuSample))
    {
        ImuLogRecord record;
        record.index         = imuSample.index;
        record.adcSampleIndex = imuSample.adcSampleIndex;
        record.ax            = imuSample.ax;
        record.ay            = imuSample.ay;
        record.az            = imuSample.az;
        record.gx            = imuSample.gx;
        record.gy            = imuSample.gy;
        record.gz            = imuSample.gz;
        writeImuRecord(record);
    }

    // Flush any remaining buffered data
    flushAdcBuffer();
    flushImuBuffer();

    // Write CRC32 checksums back to file headers before closing
    if (s_adcFile)
    {
        // Seek to CRC32 field in header (offset of dataCrc32 from start of header)
        size_t crc32Offset = offsetof(AdcLogFileHeader, dataCrc32);
        if (s_adcFile.seek(crc32Offset))
        {
            s_adcFile.write(reinterpret_cast<const uint8_t *>(&s_adcCrc32), sizeof(uint32_t));
            s_adcFile.flush();  // Ensure CRC32 is written to disk
            Serial.printf("[LOGGER] ADC CRC32 written: 0x%08X (%u records)\n", 
                         s_adcCrc32, s_writeStats.adcRecordsWritten);
        }
        else
        {
            Serial.println("[LOGGER] WARNING: Failed to seek to ADC CRC32 field");
        }
        s_adcFile.close();
    }

    if (s_imuFile)
    {
        // Seek to CRC32 field in header (offset of dataCrc32 from start of header)
        size_t crc32Offset = offsetof(ImuLogFileHeader, dataCrc32);
        if (s_imuFile.seek(crc32Offset))
        {
            s_imuFile.write(reinterpret_cast<const uint8_t *>(&s_imuCrc32), sizeof(uint32_t));
            s_imuFile.flush();  // Ensure CRC32 is written to disk
            Serial.printf("[LOGGER] IMU CRC32 written: 0x%08X (%u records)\n", 
                         s_imuCrc32, s_writeStats.imuRecordsWritten);
        }
        else
        {
            Serial.println("[LOGGER] WARNING: Failed to seek to IMU CRC32 field");
        }
        s_imuFile.close();
    }

    s_sessionOpen  = false;
    s_loggerState  = LOGGER_IDLE;
    s_adcBufferPos = 0;
    s_imuBufferPos = 0;

    LOG_DEBUG("[LOGGER] Session stopped and files closed.\n");
    LOG_DEBUG("[LOGGER] Write statistics: ADC failures=%u, IMU failures=%u\n",
             s_writeStats.adcWriteFailures, s_writeStats.imuWriteFailures);

    return true;
}

LoggerWriteStats loggerGetWriteStats()
{
    return s_writeStats;
}

// CSV conversion task (runs in separate FreeRTOS task)
static void csvConversionTask(void *param)
{
    (void)param;
    
    LOG_DEBUG("[LOGGER] CSV conversion task started\n");
    
    // Copy filenames to stack to avoid String issues
    char adcFilename[64], imuFilename[64], csvFilename[64];
    strncpy(adcFilename, s_adcFilename.c_str(), sizeof(adcFilename) - 1);
    strncpy(imuFilename, s_imuFilename.c_str(), sizeof(imuFilename) - 1);
    strncpy(csvFilename, s_csvFilename.c_str(), sizeof(csvFilename) - 1);
    adcFilename[sizeof(adcFilename) - 1] = '\0';
    imuFilename[sizeof(imuFilename) - 1] = '\0';
    csvFilename[sizeof(csvFilename) - 1] = '\0';
    
    LOG_DEBUG("[LOGGER] Converting to CSV: %s\n", csvFilename);
    LOG_DEBUG("[LOGGER] Reading ADC file: %s\n", adcFilename);
    LOG_DEBUG("[LOGGER] Reading IMU file: %s\n", imuFilename);

    // Open ADC binary file for reading
    File adcFile = s_fs->open(adcFilename, FILE_READ);
    if (!adcFile)
    {
        LOG_ERROR("[LOGGER] Failed to open ADC binary file for reading.\n");
        s_csvConversionResult = false;
        s_csvConversionInProgress = false;
        s_loggerState = LOGGER_IDLE;
        vTaskDelete(NULL);
        return;
    }

    // Read ADC header
    AdcLogFileHeader adcHdr;
    if (adcFile.read(reinterpret_cast<uint8_t *>(&adcHdr), sizeof(AdcLogFileHeader)) != sizeof(AdcLogFileHeader))
    {
        LOG_ERROR("[LOGGER] Failed to read ADC file header.\n");
        adcFile.close();
        s_csvConversionResult = false;
        s_csvConversionInProgress = false;
        s_loggerState = LOGGER_IDLE;
        vTaskDelete(NULL);
        return;
    }

    // Verify ADC magic
    if (strncmp(adcHdr.magic, "ADCLOGV1", 8) != 0)
    {
        LOG_ERROR("[LOGGER] Invalid ADC file magic.\n");
        adcFile.close();
        s_csvConversionResult = false;
        s_csvConversionInProgress = false;
        s_loggerState = LOGGER_IDLE;
        vTaskDelete(NULL);
        return;
    }

    // Open IMU binary file for reading
    File imuFile = s_fs->open(imuFilename, FILE_READ);
    if (!imuFile)
    {
        Serial.println("[LOGGER] Failed to open IMU binary file for reading.");
        adcFile.close();
        return false;
    }

    // Read IMU header
    ImuLogFileHeader imuHdr;
    if (imuFile.read(reinterpret_cast<uint8_t *>(&imuHdr), sizeof(ImuLogFileHeader)) != sizeof(ImuLogFileHeader))
    {
        LOG_ERROR("[LOGGER] Failed to read IMU file header.\n");
        adcFile.close();
        imuFile.close();
        s_csvConversionResult = false;
        s_csvConversionInProgress = false;
        s_loggerState = LOGGER_IDLE;
        vTaskDelete(NULL);
        return;
    }

    // Verify IMU magic
    if (strncmp(imuHdr.magic, "IMULOGV1", 8) != 0)
    {
        LOG_ERROR("[LOGGER] Invalid IMU file magic.\n");
        adcFile.close();
        imuFile.close();
        s_csvConversionResult = false;
        s_csvConversionInProgress = false;
        s_loggerState = LOGGER_IDLE;
        vTaskDelete(NULL);
        return;
    }

    // Verify CRC32 checksums for data integrity
    LOG_DEBUG("[LOGGER] Verifying CRC32 checksums...\n");
    
    // Calculate CRC32 for ADC data (skip header)
    uint32_t calculatedAdcCrc = 0;
    size_t adcDataSize = adcFile.size() - sizeof(AdcLogFileHeader);
    adcFile.seek(sizeof(AdcLogFileHeader));  // Skip header
    const size_t CRC_BUFFER_SIZE = 4096;
    uint8_t crcBuffer[CRC_BUFFER_SIZE];
    size_t bytesRead = 0;
    while (bytesRead < adcDataSize)
    {
        size_t toRead = (adcDataSize - bytesRead > CRC_BUFFER_SIZE) ? CRC_BUFFER_SIZE : (adcDataSize - bytesRead);
        size_t read = adcFile.read(crcBuffer, toRead);
        if (read == 0) break;
        calculatedAdcCrc = esp_crc32_le(calculatedAdcCrc, crcBuffer, read);
        bytesRead += read;
    }
    
    if (calculatedAdcCrc != adcHdr.dataCrc32)
    {
        LOG_ERROR("[LOGGER] WARNING: ADC file CRC32 mismatch! Expected: 0x%08X, Calculated: 0x%08X\n",
                 adcHdr.dataCrc32, calculatedAdcCrc);
        // Continue with warning - data may be corrupted but user should know
    }
    else
    {
        LOG_DEBUG("[LOGGER] ADC CRC32 verified: 0x%08X\n", calculatedAdcCrc);
    }
    
    // Calculate CRC32 for IMU data (skip header)
    uint32_t calculatedImuCrc = 0;
    size_t imuDataSize = imuFile.size() - sizeof(ImuLogFileHeader);
    imuFile.seek(sizeof(ImuLogFileHeader));  // Skip header
    bytesRead = 0;
    while (bytesRead < imuDataSize)
    {
        size_t toRead = (imuDataSize - bytesRead > CRC_BUFFER_SIZE) ? CRC_BUFFER_SIZE : (imuDataSize - bytesRead);
        size_t read = imuFile.read(crcBuffer, toRead);
        if (read == 0) break;
        calculatedImuCrc = esp_crc32_le(calculatedImuCrc, crcBuffer, read);
        bytesRead += read;
    }
    
    if (calculatedImuCrc != imuHdr.dataCrc32)
    {
        LOG_ERROR("[LOGGER] WARNING: IMU file CRC32 mismatch! Expected: 0x%08X, Calculated: 0x%08X\n",
                 imuHdr.dataCrc32, calculatedImuCrc);
        // Continue with warning
    }
    else
    {
        LOG_DEBUG("[LOGGER] IMU CRC32 verified: 0x%08X\n", calculatedImuCrc);
    }
    
    // Reset file positions to start of data
    adcFile.seek(sizeof(AdcLogFileHeader));
    imuFile.seek(sizeof(ImuLogFileHeader));
    
    // Pre-read IMU records into a buffer to avoid backward seeks
    // Use a reasonable buffer size (100 records = ~2.8KB)
    const size_t IMU_BUFFER_SIZE = 100;
    struct ImuBufferEntry {
        uint32_t adcSampleIndex;
        ImuLogRecord record;
    };
    ImuBufferEntry imuBuffer[IMU_BUFFER_SIZE];
    size_t imuBufferCount = 0;
    size_t imuBufferIndex = 0;
    
    LOG_DEBUG("[LOGGER] Pre-reading IMU records into buffer...\n");
    while (imuBufferCount < IMU_BUFFER_SIZE && imuFile.available() >= sizeof(ImuLogRecord))
    {
        if (imuFile.read(reinterpret_cast<uint8_t *>(&imuBuffer[imuBufferCount].record), 
                         sizeof(ImuLogRecord)) == sizeof(ImuLogRecord))
        {
            imuBuffer[imuBufferCount].adcSampleIndex = imuBuffer[imuBufferCount].record.adcSampleIndex;
            imuBufferCount++;
        }
        else
        {
            break;
        }
    }
    LOG_DEBUG("[LOGGER] Pre-read %u IMU records\n", imuBufferCount);
    
    // Open CSV file for writing
    File csvFile = s_fs->open(csvFilename, FILE_WRITE);
    if (!csvFile)
    {
        LOG_ERROR("[LOGGER] Failed to open CSV file for writing.\n");
        adcFile.close();
        imuFile.close();
        s_csvConversionResult = false;
        s_csvConversionInProgress = false;
        s_loggerState = LOGGER_IDLE;
        vTaskDelete(NULL);
        return;
    }

    // Write CSV header (using single print call)
    csvFile.print("ADC_Index,Time_Seconds,ADC_Code,IMU_Index,AX,AY,AZ,GX,GY,GZ\n");

    // Initialize timebase for time calculation
    SampleTimebase timebase;
    RtcDateTime anchorRtc;
    anchorRtc.year    = adcHdr.rtcYear;
    anchorRtc.month   = adcHdr.rtcMonth;
    anchorRtc.day     = adcHdr.rtcDay;
    anchorRtc.hour    = adcHdr.rtcHour;
    anchorRtc.minute  = adcHdr.rtcMinute;
    anchorRtc.second  = adcHdr.rtcSecond;
    anchorRtc.weekday = adcHdr.rtcWeekday;
    
    rtcInitSampleTimebase(timebase, anchorRtc, adcHdr.adcIndexAtStart, adcHdr.adcSampleRate);

    // Find current IMU record from buffer (forward-fill strategy)
    ImuLogRecord *currentImu = nullptr;
    size_t currentImuIdx = 0;
    
    // Find the most recent IMU record that's <= first ADC index
    for (size_t i = 0; i < imuBufferCount; i++)
    {
        if (imuBuffer[i].adcSampleIndex <= adcHdr.adcIndexAtStart)
        {
            currentImu = &imuBuffer[i].record;
            currentImuIdx = i;
        }
        else
        {
            break;  // Buffer is sorted by adcSampleIndex
        }
    }

    // Process ADC records and align with IMU
    LOG_DEBUG("[LOGGER] Processing ADC records and aligning with IMU...\n");
    uint32_t recordCount = 0;
    const size_t recordSize = sizeof(AdcLogRecord);
    const size_t RECORDS_PER_YIELD = 1000;  // Process 1000 records, then yield
    uint32_t lastNeopixelUpdate = millis();
    uint32_t lastProgressLog = millis();
    
    // CSV line buffer (optimized: single snprintf call per record)
    char csvLine[256];
    
    while (adcFile.available() >= recordSize)
    {
        AdcLogRecord adcRec;
        if (adcFile.read(reinterpret_cast<uint8_t *>(&adcRec), recordSize) != recordSize)
        {
            break;
        }

        // Advance IMU buffer pointer to find the most recent IMU sample <= current ADC index
        // First, check if we need to refill the buffer
        while (currentImuIdx + 1 < imuBufferCount && 
               imuBuffer[currentImuIdx + 1].adcSampleIndex <= adcRec.index)
        {
            currentImuIdx++;
            currentImu = &imuBuffer[currentImuIdx].record;
        }
        
        // If we've consumed most of the buffer, refill it
        if (currentImuIdx >= IMU_BUFFER_SIZE - 10 && imuFile.available() >= sizeof(ImuLogRecord))
        {
            // Shift remaining entries to start of buffer
            size_t remaining = imuBufferCount - currentImuIdx;
            if (remaining > 0 && remaining < IMU_BUFFER_SIZE)
            {
                memmove(imuBuffer, &imuBuffer[currentImuIdx], remaining * sizeof(ImuBufferEntry));
            }
            
            // Read new records to fill buffer
            size_t toRead = IMU_BUFFER_SIZE - remaining;
            for (size_t i = 0; i < toRead && imuFile.available() >= sizeof(ImuLogRecord); i++)
            {
                if (imuFile.read(reinterpret_cast<uint8_t *>(&imuBuffer[remaining + i].record),
                                 sizeof(ImuLogRecord)) == sizeof(ImuLogRecord))
                {
                    imuBuffer[remaining + i].adcSampleIndex = imuBuffer[remaining + i].record.adcSampleIndex;
                    imuBufferCount = remaining + i + 1;
                }
                else
                {
                    break;
                }
            }
            currentImuIdx = 0;
            if (imuBufferCount > 0)
            {
                currentImu = &imuBuffer[0].record;
            }
            else
            {
                currentImu = nullptr;
            }
        }

        // Calculate time in seconds
        double timeSeconds = rtcSampleIndexToSeconds(timebase, adcRec.index);

        // Build CSV line using snprintf (single call instead of multiple prints)
        if (currentImu && currentImu->adcSampleIndex <= adcRec.index)
        {
            snprintf(csvLine, sizeof(csvLine),
                "%u,%.6f,%ld,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                adcRec.index,
                timeSeconds,
                adcRec.code,
                currentImu->index,
                currentImu->ax,
                currentImu->ay,
                currentImu->az,
                currentImu->gx,
                currentImu->gy,
                currentImu->gz);
        }
        else
        {
            // No IMU data available - leave blank
            snprintf(csvLine, sizeof(csvLine),
                "%u,%.6f,%ld,0,0.0,0.0,0.0,0.0,0.0,0.0\n",
                adcRec.index,
                timeSeconds,
                adcRec.code);
        }
        
        // Write CSV line (single write instead of 10+ writes)
        csvFile.print(csvLine);

        recordCount++;
        
        // Incremental processing: yield periodically to allow other tasks
        if (recordCount % RECORDS_PER_YIELD == 0)
        {
            // Update neopixel
            uint32_t now = millis();
            if (now - lastNeopixelUpdate >= 100)
            {
                neopixelUpdate();
                lastNeopixelUpdate = now;
            }
            
            // Yield to other tasks (web server, button handling, etc.)
            vTaskDelay(pdMS_TO_TICKS(1));
            
            // Progress indicator (rate-limited)
            if (now - lastProgressLog >= 5000)  // Every 5 seconds
            {
                LOG_DEBUG("[LOGGER] Processed %u ADC records...\n", recordCount);
                lastProgressLog = now;
            }
        }
    }

    csvFile.flush();
    csvFile.close();
    adcFile.close();
    imuFile.close();

    LOG_DEBUG("[LOGGER] CSV conversion complete. Wrote %u records to %s\n", recordCount, csvFilename);
    
    s_csvConversionResult = true;
    s_csvConversionInProgress = false;
    s_loggerState = LOGGER_IDLE;
    
    // Task completes and deletes itself
    vTaskDelete(NULL);
}

bool loggerConvertLastSessionToCsv()
{
    if (!s_hasLastSession)
    {
        LOG_ERROR("[LOGGER] No last session available to convert.\n");
        return false;
    }

    if (!sdCardIsMounted())
    {
        LOG_ERROR("[LOGGER] Cannot convert: SD card is not mounted.\n");
        return false;
    }

    if (s_sessionOpen)
    {
        LOG_ERROR("[LOGGER] Cannot convert: session is still open. Stop session first.\n");
        return false;
    }

    if (s_csvConversionInProgress)
    {
        LOG_ERROR("[LOGGER] CSV conversion already in progress.\n");
        return false;
    }

    s_fs = &sdCardGetFs();
    s_loggerState = LOGGER_CONVERTING;
    s_csvConversionInProgress = true;
    s_csvConversionResult = false;

    // Create FreeRTOS task for CSV conversion (non-blocking)
    BaseType_t result = xTaskCreatePinnedToCore(
        csvConversionTask,
        "CsvConvert",
        16384,  // 16KB stack for file operations
        nullptr,
        configMAX_PRIORITIES - 3,  // Lower priority than sampling tasks
        &s_csvTaskHandle,
        1  // Core 1 (same as main loop)
    );

    if (result != pdPASS)
    {
        LOG_ERROR("[LOGGER] Failed to create CSV conversion task!\n");
        s_csvConversionInProgress = false;
        s_loggerState = LOGGER_IDLE;
        return false;
    }

    LOG_DEBUG("[LOGGER] CSV conversion task created successfully\n");
    return true;  // Task is running, result will be available later
}

// Check if CSV conversion is complete and get result
bool loggerIsCsvConversionComplete(bool *result)
{
    if (!s_csvConversionInProgress)
    {
        if (result) *result = s_csvConversionResult;
        return true;  // Conversion complete (or never started)
    }
    return false;  // Still in progress
}

bool loggerHasLastSession()
{
    return s_hasLastSession;
}

String loggerGetCurrentBaseName()
{
    return s_baseName;
}

String loggerGetCurrentAdcFilename()
{
    return s_adcFilename;
}

String loggerGetCurrentImuFilename()
{
    return s_imuFilename;
}

String loggerGetCurrentCsvFilename()
{
    return s_csvFilename;
}
