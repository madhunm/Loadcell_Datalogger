#include "logger.h"

#include "sdcard.h"
#include "rtc.h"
#include "adc.h"
#include "imu.h"
#include "neopixel.h"

#include "FS.h"
#include <cstring>

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

// Write buffering for efficiency
static const size_t  WRITE_BUFFER_SIZE    = 8192;   // 8 KB buffer per file
static uint8_t       s_adcWriteBuffer[WRITE_BUFFER_SIZE];
static uint8_t       s_imuWriteBuffer[WRITE_BUFFER_SIZE];
static size_t        s_adcBufferPos        = 0;
static size_t        s_imuBufferPos        = 0;

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

    // reserved2[] already zeroed by memset()
}

// Flush ADC write buffer to file
static void flushAdcBuffer()
{
    if (s_adcBufferPos > 0 && s_adcFile)
    {
        s_adcFile.write(s_adcWriteBuffer, s_adcBufferPos);
        s_adcBufferPos = 0;
    }
}

// Flush IMU write buffer to file
static void flushImuBuffer()
{
    if (s_imuBufferPos > 0 && s_imuFile)
    {
        s_imuFile.write(s_imuWriteBuffer, s_imuBufferPos);
        s_imuBufferPos = 0;
    }
}

// Write ADC record to buffer (flushes buffer if full)
static bool writeAdcRecord(const AdcLogRecord &record)
{
    const size_t recordSize = sizeof(AdcLogRecord);
    
    // Check if record fits in remaining buffer space
    if (s_adcBufferPos + recordSize > WRITE_BUFFER_SIZE)
    {
        flushAdcBuffer();
    }
    
    // Copy record to buffer
    memcpy(&s_adcWriteBuffer[s_adcBufferPos], &record, recordSize);
    s_adcBufferPos += recordSize;
    
    return true;
}

// Write IMU record to buffer (flushes buffer if full)
static bool writeImuRecord(const ImuLogRecord &record)
{
    const size_t recordSize = sizeof(ImuLogRecord);
    
    // Check if record fits in remaining buffer space
    if (s_imuBufferPos + recordSize > WRITE_BUFFER_SIZE)
    {
        flushImuBuffer();
    }
    
    // Copy record to buffer
    memcpy(&s_imuWriteBuffer[s_imuBufferPos], &record, recordSize);
    s_imuBufferPos += recordSize;
    
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
}

bool loggerStartSession(const LoggerConfig &config)
{
    // Ensure SD card is mounted
    if (!sdCardIsMounted())
    {
        Serial.println("[LOGGER] Cannot start session: SD card is not mounted.");
        return false;
    }

    // Get filesystem
    s_fs = &sdCardGetFs();

    // Read current RTC time
    RtcDateTime nowRtc;
    if (!rtcGetDateTime(nowRtc))
    {
        Serial.println("[LOGGER] Cannot read RTC date/time; aborting session start.");
        return false;
    }

    // Determine ADC sample index at start
    uint32_t adcIndexNow = adcGetSampleCounter();

    // Generate base name from RTC
    String baseName = makeBaseNameFromRtc(nowRtc);
    if (baseName.length() == 0)
    {
        Serial.println("[LOGGER] Failed to generate base name from RTC.");
        return false;
    }

    // Build filenames (use /log directory)
    String adcFilename = "/log/" + baseName + "_ADC.bin";
    String imuFilename = "/log/" + baseName + "_IMU.bin";
    String csvFilename = "/log/" + baseName + ".csv";

    Serial.print("[LOGGER] Starting session with base name: ");
    Serial.println(baseName);
    Serial.print("[LOGGER] ADC log file: ");
    Serial.println(adcFilename);
    Serial.print("[LOGGER] IMU log file: ");
    Serial.println(imuFilename);

    // Open ADC binary file for write (truncate if exists)
    File adcFile = s_fs->open(adcFilename, FILE_WRITE);
    if (!adcFile)
    {
        Serial.println("[LOGGER] Failed to open ADC binary log file for writing.");
        return false;
    }

    // Open IMU binary file for write (truncate if exists)
    File imuFile = s_fs->open(imuFilename, FILE_WRITE);
    if (!imuFile)
    {
        Serial.println("[LOGGER] Failed to open IMU binary log file for writing.");
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
        Serial.println("[LOGGER] Failed to write full ADC log header; aborting.");
        adcFile.close();
        imuFile.close();
        return false;
    }

    if (imuWritten != sizeof(ImuLogFileHeader))
    {
        Serial.println("[LOGGER] Failed to write full IMU log header; aborting.");
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

    s_baseName         = baseName;
    s_adcFilename      = adcFilename;
    s_imuFilename      = imuFilename;
    s_csvFilename      = csvFilename;

    s_adcBufferPos     = 0;
    s_imuBufferPos     = 0;

    s_sessionOpen      = true;
    s_hasLastSession   = true;
    s_loggerState      = LOGGER_SESSION_OPEN;

    Serial.println("[LOGGER] Session started and headers written to both files.");

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
            Serial.println("[LOGGER] ERROR: Failed to write ADC record!");
            // Check if SD card was removed
            if (!sdCardCheckPresent())
            {
                Serial.println("[LOGGER] SD card removed - stopping session");
                loggerStopSessionAndFlush();
                s_loggerState = LOGGER_IDLE;
                return;
            }
            // Continue anyway to avoid blocking (will retry on next tick)
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
            Serial.println("[LOGGER] ERROR: Failed to write IMU record!");
            // Check if SD card was removed
            if (!sdCardCheckPresent())
            {
                Serial.println("[LOGGER] SD card removed - stopping session");
                loggerStopSessionAndFlush();
                s_loggerState = LOGGER_IDLE;
                return;
            }
            // Continue anyway to avoid blocking
        }
        imuSamplesProcessed++;
    }

    // Periodic flush (every ~100ms worth of data or when buffers are getting full)
    // For 64k samples/sec ADC, 100ms = 6400 samples = ~51KB, so flush frequently
    static uint32_t lastFlushMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastFlushMs > 100)  // Flush every 100ms
    {
        flushAdcBuffer();
        flushImuBuffer();
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

    Serial.println("[LOGGER] Stopping session: draining buffers, flushing, and closing files.");

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

    // Flush and close files
    if (s_adcFile)
    {
        s_adcFile.flush();
        s_adcFile.close();
    }

    if (s_imuFile)
    {
        s_imuFile.flush();
        s_imuFile.close();
    }

    s_sessionOpen  = false;
    s_loggerState  = LOGGER_IDLE;
    s_adcBufferPos = 0;
    s_imuBufferPos = 0;

    Serial.println("[LOGGER] Session stopped and files closed.");

    return true;
}

bool loggerConvertLastSessionToCsv()
{
    if (!s_hasLastSession)
    {
        Serial.println("[LOGGER] No last session available to convert.");
        return false;
    }

    if (!sdCardIsMounted())
    {
        Serial.println("[LOGGER] Cannot convert: SD card is not mounted.");
        return false;
    }

    if (s_sessionOpen)
    {
        Serial.println("[LOGGER] Cannot convert: session is still open. Stop session first.");
        return false;
    }

    s_fs = &sdCardGetFs();
    s_loggerState = LOGGER_CONVERTING;

    Serial.print("[LOGGER] Converting to CSV: ");
    Serial.println(s_csvFilename);
    Serial.print("[LOGGER] Reading ADC file: ");
    Serial.println(s_adcFilename);
    Serial.print("[LOGGER] Reading IMU file: ");
    Serial.println(s_imuFilename);

    // Open ADC binary file for reading
    File adcFile = s_fs->open(s_adcFilename, FILE_READ);
    if (!adcFile)
    {
        Serial.println("[LOGGER] Failed to open ADC binary file for reading.");
        return false;
    }

    // Read ADC header
    AdcLogFileHeader adcHdr;
    if (adcFile.read(reinterpret_cast<uint8_t *>(&adcHdr), sizeof(AdcLogFileHeader)) != sizeof(AdcLogFileHeader))
    {
        Serial.println("[LOGGER] Failed to read ADC file header.");
        adcFile.close();
        return false;
    }

    // Verify ADC magic
    if (strncmp(adcHdr.magic, "ADCLOGV1", 8) != 0)
    {
        Serial.println("[LOGGER] Invalid ADC file magic.");
        adcFile.close();
        return false;
    }

    // Open IMU binary file for reading
    File imuFile = s_fs->open(s_imuFilename, FILE_READ);
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
        Serial.println("[LOGGER] Failed to read IMU file header.");
        adcFile.close();
        imuFile.close();
        return false;
    }

    // Verify IMU magic
    if (strncmp(imuHdr.magic, "IMULOGV1", 8) != 0)
    {
        Serial.println("[LOGGER] Invalid IMU file magic.");
        adcFile.close();
        imuFile.close();
        return false;
    }

    // Build IMU sample index map: adcSampleIndex -> ImuLogRecord
    // We'll use a simple approach: read all IMU records and store them in a map
    // For large files, we might need a more sophisticated approach, but for now this works
    Serial.println("[LOGGER] Reading IMU records...");
    
    // Simple map: we'll use an array indexed by (adcSampleIndex - adcHdr.adcIndexAtStart)
    // But this could be huge, so let's use a different approach: forward-fill as we go
    
    // Open CSV file for writing
    File csvFile = s_fs->open(s_csvFilename, FILE_WRITE);
    if (!csvFile)
    {
        Serial.println("[LOGGER] Failed to open CSV file for writing.");
        adcFile.close();
        imuFile.close();
        return false;
    }

    // Write CSV header
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

    // Read IMU records into a simple forward-fill buffer
    // Strategy: Read IMU records sequentially, and forward-fill them to matching ADC samples
    ImuLogRecord currentImu;
    bool hasCurrentImu = false;
    uint32_t currentImuAdcIndex = 0;
    
    // Read first IMU record if available
    if (imuFile.available() >= sizeof(ImuLogRecord))
    {
        if (imuFile.read(reinterpret_cast<uint8_t *>(&currentImu), sizeof(ImuLogRecord)) == sizeof(ImuLogRecord))
        {
            hasCurrentImu = true;
            currentImuAdcIndex = currentImu.adcSampleIndex;
        }
    }

    // Process ADC records and align with IMU
    Serial.println("[LOGGER] Processing ADC records and aligning with IMU...");
    uint32_t recordCount = 0;
    const size_t recordSize = sizeof(AdcLogRecord);
    uint32_t lastNeopixelUpdate = millis();
    
    while (adcFile.available() >= recordSize)
    {
        AdcLogRecord adcRec;
        if (adcFile.read(reinterpret_cast<uint8_t *>(&adcRec), recordSize) != recordSize)
        {
            break;
        }

        // Advance IMU pointer to find the most recent IMU sample that's <= current ADC index
        // This implements forward-fill: each ADC sample gets the most recent IMU data
        // Read ahead to find the latest IMU that applies to this ADC sample
        while (imuFile.available() >= sizeof(ImuLogRecord))
        {
            ImuLogRecord nextImu;
            size_t pos = imuFile.position();
            
            if (imuFile.read(reinterpret_cast<uint8_t *>(&nextImu), sizeof(ImuLogRecord)) != sizeof(ImuLogRecord))
            {
                break;
            }
            
            // If next IMU's adcSampleIndex is <= current ADC index, it's more recent, use it
            if (nextImu.adcSampleIndex <= adcRec.index)
            {
                currentImu = nextImu;
                currentImuAdcIndex = currentImu.adcSampleIndex;
                hasCurrentImu = true;
                // Continue to check if there's an even more recent one
            }
            else
            {
                // Next IMU is in the future, rewind and keep current one
                imuFile.seek(pos);
                break;
            }
        }

        // Calculate time in seconds
        double timeSeconds = rtcSampleIndexToSeconds(timebase, adcRec.index);

        // Write CSV row
        csvFile.print(adcRec.index);
        csvFile.print(",");
        csvFile.print(timeSeconds, 6);  // 6 decimal places for microsecond precision
        csvFile.print(",");
        csvFile.print(adcRec.code);
        csvFile.print(",");

        // Write IMU data if we have a current IMU sample that matches or precedes this ADC sample
        if (hasCurrentImu && currentImuAdcIndex <= adcRec.index)
        {
            csvFile.print(currentImu.index);
            csvFile.print(",");
            csvFile.print(currentImu.ax, 6);
            csvFile.print(",");
            csvFile.print(currentImu.ay, 6);
            csvFile.print(",");
            csvFile.print(currentImu.az, 6);
            csvFile.print(",");
            csvFile.print(currentImu.gx, 6);
            csvFile.print(",");
            csvFile.print(currentImu.gy, 6);
            csvFile.print(",");
            csvFile.print(currentImu.gz, 6);
        }
        else
        {
            // No IMU data available - leave blank (7 empty fields: IMU_Index, AX, AY, AZ, GX, GY, GZ)
            csvFile.print(",,,,,,,");  // 7 commas for 7 empty fields
        }
        csvFile.print("\n");

        recordCount++;
        
        // Update neopixel periodically to keep blinking pattern active during conversion
        uint32_t now = millis();
        if (now - lastNeopixelUpdate >= 100)  // Update every 100ms
        {
            neopixelUpdate();
            lastNeopixelUpdate = now;
        }
        
        // Progress indicator every 100k records
        if (recordCount % 100000 == 0)
        {
            Serial.printf("[LOGGER] Processed %u ADC records...\n", recordCount);
        }
    }

    csvFile.flush();
    csvFile.close();
    adcFile.close();
    imuFile.close();

    Serial.printf("[LOGGER] CSV conversion complete. Wrote %u records to %s\n", recordCount, s_csvFilename.c_str());
    
    s_loggerState = LOGGER_IDLE;
    return true;
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
