/**
 * @file logger_module.cpp
 * @brief Implementation of high-rate data logger
 */

#include "logger_module.h"
#include "../drivers/rx8900ce_driver.h"

// Static instance for ISR
LoggerModule* LoggerModule::instance = nullptr;

void IRAM_ATTR LoggerModule::adcReadyISR() {
    if (!instance || !instance->logging) {
        return;
    }
    
    // Read ADC value (fast SPI read)
    int32_t adc_raw = instance->adc_driver->readRawFast();
    uint32_t timestamp = instance->timestamp_sync->getRelativeMicroseconds();
    
    // Check for ring buffer overflow
    uint32_t next_write = (instance->write_index + 1) % MAX_RING_ENTRIES;
    
    if (next_write == instance->read_index) {
        instance->stats.buffer_overruns++;
        return;  // Buffer full, drop sample
    }
    
    // Add to ring buffer
    RingBufferEntry& entry = instance->ring_buffer[instance->write_index];
    entry.timestamp_us = timestamp;
    entry.adc_value = adc_raw;
    entry.has_imu = false;
    
    // Check if we need to read IMU (every 64th sample)
    instance->adc_sample_count++;
    if ((instance->adc_sample_count % IMU_DECIMATION) == 0) {
        // Read IMU in same ISR context for perfect sync
        if (instance->imu_driver->readDataFast(entry.imu)) {
            entry.imu.timestamp_offset_us = timestamp;
            entry.has_imu = true;
            instance->stats.imu_samples++;
        }
    }
    
    // Advance write pointer
    instance->write_index = next_write;
    instance->stats.samples_acquired++;
}

void LoggerModule::writerTask(void* param) {
    LoggerModule* logger = (LoggerModule*)param;
    
    while (logger->logging) {
        // Drain ring buffer to active write buffer
        logger->drainRingBuffer();
        
        // If active buffer is getting full, swap and write
        if (logger->active_buffer_pos >= WRITE_BUFFER_SIZE - 256) {
            if (!logger->flushWriteBuffer()) {
                logger->stats.write_errors++;
            }
        }
        
        // Small delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Final flush before exit
    logger->flushWriteBuffer();
    
    vTaskDelete(NULL);
}

bool LoggerModule::begin(MAX11270Driver* adc, LSM6DSVDriver* imu,
                         SDManager* sd, TimestampSync* ts) {
    instance = this;
    adc_driver = adc;
    imu_driver = imu;
    sd_manager = sd;
    timestamp_sync = ts;
    
    logging = false;
    write_index = 0;
    read_index = 0;
    adc_sample_count = 0;
    
    active_buffer = write_buffer_a;
    buffer_a_active = true;
    active_buffer_pos = 0;
    
    memset(&stats, 0, sizeof(stats));
    writer_task_handle = NULL;
    
    return true;
}

String LoggerModule::generateLogFilename() {
    // Generate filename based on RTC time
    // Format: /data/log_YYYYMMDD_HHMMSS.bin
    
    DateTime dt;
    if (timestamp_sync) {
        uint32_t unix_time = timestamp_sync->getMicroseconds() / 1000000ULL;
        dt.fromUnixTime(unix_time);
    } else {
        // Fallback to millis-based name
        return String("/data/log_") + String(millis()) + String(".bin");
    }
    
    char filename[64];
    snprintf(filename, sizeof(filename), "/data/log_%04d%02d%02d_%02d%02d%02d.bin",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    
    return String(filename);
}

bool LoggerModule::writeHeader(const LoadcellCalibration& cal) {
    LogFileHeader header;
    
    header.sample_rate_hz = 64000;
    header.imu_rate_hz = 1000;
    header.start_timestamp_us = timestamp_sync->getMicroseconds();
    strncpy(header.loadcell_id, cal.id, sizeof(header.loadcell_id) - 1);
    
    size_t written = log_file.write((uint8_t*)&header, sizeof(header));
    return (written == sizeof(header));
}

bool LoggerModule::startLogging(const LoadcellCalibration& loadcell_cal) {
    if (logging) {
        return false;  // Already logging
    }
    
    // Ensure SD is mounted
    if (!sd_manager->isMounted()) {
        Serial.println("Logger: SD not mounted");
        return false;
    }
    
    // Create data directory if needed
    sd_manager->createDirectory("/data");
    
    // Generate filename and open file
    current_log_file = generateLogFilename();
    log_file = sd_manager->openWrite(current_log_file.c_str(), false);
    
    if (!log_file) {
        Serial.println("Logger: Failed to open log file");
        return false;
    }
    
    // Write header
    if (!writeHeader(loadcell_cal)) {
        Serial.println("Logger: Failed to write header");
        log_file.close();
        return false;
    }
    
    // Reset state
    write_index = 0;
    read_index = 0;
    adc_sample_count = 0;
    active_buffer_pos = 0;
    memset(&stats, 0, sizeof(stats));
    
    // Start timestamp tracking
    timestamp_sync->startLogging();
    
    // Start ADC continuous conversion
    if (!adc_driver->startContinuous()) {
        Serial.println("Logger: Failed to start ADC");
        log_file.close();
        return false;
    }
    
    logging = true;
    
    // Attach ADC ready interrupt
    pinMode(PIN_ADC_RDYB, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_ADC_RDYB), adcReadyISR, FALLING);
    
    // Create writer task on Core 0
    xTaskCreatePinnedToCore(
        writerTask,
        "LogWriter",
        4096,           // Stack size
        this,           // Parameter
        configMAX_PRIORITIES - 1,  // High priority
        &writer_task_handle,
        0               // Core 0
    );
    
    Serial.printf("Logger: Started logging to %s\n", current_log_file.c_str());
    
    return true;
}

bool LoggerModule::stopLogging() {
    if (!logging) {
        return true;
    }
    
    // Stop ADC interrupt
    detachInterrupt(digitalPinToInterrupt(PIN_ADC_RDYB));
    
    // Stop ADC conversion
    adc_driver->stopContinuous();
    
    // Signal writer task to stop
    logging = false;
    
    // Wait for writer task to finish
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Final buffer drain
    drainRingBuffer();
    flushWriteBuffer();
    
    // Close log file
    if (log_file) {
        log_file.close();
    }
    
    Serial.printf("Logger: Stopped. %u samples, %u IMU samples\n",
                  stats.samples_acquired, stats.imu_samples);
    Serial.printf("Logger: Overruns: %u, Errors: %u\n",
                  stats.buffer_overruns, stats.write_errors);
    
    return true;
}

void LoggerModule::drainRingBuffer() {
    while (read_index != write_index) {
        RingBufferEntry& entry = ring_buffer[read_index];
        
        // Write loadcell sample
        LoadcellSample lc_sample;
        lc_sample.timestamp_offset_us = entry.timestamp_us;
        lc_sample.raw_adc = entry.adc_value;
        
        // Check if we have space in active buffer
        if (active_buffer_pos + sizeof(LoadcellSample) <= WRITE_BUFFER_SIZE) {
            memcpy(active_buffer + active_buffer_pos, &lc_sample, sizeof(LoadcellSample));
            active_buffer_pos += sizeof(LoadcellSample);
            stats.samples_written++;
        }
        
        // Write IMU sample if present
        if (entry.has_imu) {
            if (active_buffer_pos + sizeof(IMUSample) <= WRITE_BUFFER_SIZE) {
                memcpy(active_buffer + active_buffer_pos, &entry.imu, sizeof(IMUSample));
                active_buffer_pos += sizeof(IMUSample);
            }
        }
        
        // Advance read pointer
        read_index = (read_index + 1) % MAX_RING_ENTRIES;
    }
    
    // Calculate fill percentage
    uint32_t fill = getRingBufferFill();
    stats.fill_percent = (fill * 100.0f) / MAX_RING_ENTRIES;
}

bool LoggerModule::flushWriteBuffer() {
    if (active_buffer_pos == 0) {
        return true;  // Nothing to write
    }
    
    if (!log_file) {
        return false;
    }
    
    // Write active buffer to SD
    size_t written = log_file.write(active_buffer, active_buffer_pos);
    log_file.flush();
    
    // Swap buffers
    if (buffer_a_active) {
        active_buffer = write_buffer_b;
        buffer_a_active = false;
    } else {
        active_buffer = write_buffer_a;
        buffer_a_active = true;
    }
    
    active_buffer_pos = 0;
    
    return (written > 0);
}

uint32_t LoggerModule::getRingBufferFill() const {
    if (write_index >= read_index) {
        return write_index - read_index;
    } else {
        return MAX_RING_ENTRIES - read_index + write_index;
    }
}
