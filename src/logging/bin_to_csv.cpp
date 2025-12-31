/**
 * @file bin_to_csv.cpp
 * @brief Implementation of binary to CSV converter
 */

#include "bin_to_csv.h"

bool BinToCSVConverter::begin(SDManager* sd, CalibrationInterp* interp) {
    sd_manager = sd;
    calibration_interp = interp;
    memset(&stats, 0, sizeof(stats));
    return true;
}

String BinToCSVConverter::generateCSVFilename(const char* bin_path) {
    String path(bin_path);
    
    // Replace .bin with .csv
    int dot_pos = path.lastIndexOf('.');
    if (dot_pos > 0) {
        path = path.substring(0, dot_pos) + ".csv";
    } else {
        path += ".csv";
    }
    
    return path;
}

String BinToCSVConverter::timestampToISO(uint64_t timestamp_us) {
    // Convert microseconds since epoch to ISO 8601 format
    uint32_t unix_sec = timestamp_us / 1000000ULL;
    uint32_t frac_us = timestamp_us % 1000000ULL;
    
    DateTime dt;
    dt.fromUnixTime(unix_sec);
    
    char iso[32];
    snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d.%06luZ",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, (unsigned long)frac_us);
    
    return String(iso);
}

bool BinToCSVConverter::writeCSVHeader(File& csv_file, const LogFileHeader& header) {
    // Write metadata as comments
    csv_file.printf("# Loadcell Data Log\n");
    csv_file.printf("# Loadcell ID: %s\n", header.loadcell_id);
    csv_file.printf("# Sample Rate: %u Hz\n", header.sample_rate_hz);
    csv_file.printf("# IMU Rate: %u Hz\n", header.imu_rate_hz);
    csv_file.printf("# Start Time: %s\n", timestampToISO(header.start_timestamp_us).c_str());
    csv_file.printf("#\n");
    
    // Write column headers
    csv_file.printf("timestamp_us,timestamp_iso,sample_type,");
    csv_file.printf("raw_adc,load_kg,");
    csv_file.printf("accel_x,accel_y,accel_z,");
    csv_file.printf("gyro_x,gyro_y,gyro_z\n");
    
    return true;
}

bool BinToCSVConverter::convert(const char* bin_path, void (*progress_callback)(int)) {
    memset(&stats, 0, sizeof(stats));
    uint32_t start_time = millis();
    
    // Open binary file
    File bin_file = sd_manager->openRead(bin_path);
    if (!bin_file) {
        Serial.printf("CSV: Failed to open %s\n", bin_path);
        return false;
    }
    
    size_t file_size = bin_file.size();
    
    // Read and validate header
    LogFileHeader header;
    if (bin_file.read((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        Serial.println("CSV: Failed to read header");
        bin_file.close();
        return false;
    }
    
    if (header.magic != LOG_MAGIC) {
        Serial.printf("CSV: Invalid magic number: 0x%08X\n", header.magic);
        bin_file.close();
        return false;
    }
    
    // Generate CSV filename and open file
    last_csv_path = generateCSVFilename(bin_path);
    File csv_file = sd_manager->openWrite(last_csv_path.c_str(), false);
    
    if (!csv_file) {
        Serial.printf("CSV: Failed to create %s\n", last_csv_path.c_str());
        bin_file.close();
        return false;
    }
    
    // Write CSV header
    writeCSVHeader(csv_file, header);
    
    // Process records - read loadcell samples (8 bytes each)
    int last_progress = -1;
    LoadcellSample lc_sample;
    IMUSample imu_sample;
    uint32_t sample_count = 0;
    
    while (bin_file.available() >= (int)sizeof(LoadcellSample)) {
        // Read loadcell sample
        if (bin_file.read((uint8_t*)&lc_sample, sizeof(lc_sample)) == sizeof(lc_sample)) {
            uint64_t abs_timestamp = header.start_timestamp_us + lc_sample.timestamp_offset_us;
            
            // Convert raw ADC to microvolts, then to kg
            float uV = 20000000.0f * lc_sample.raw_adc / 8388608.0f;  // Placeholder conversion
            float load_kg = calibration_interp->isCalibrated() ?
                          calibration_interp->convertToKg(uV) : 0;
            
            // Write CSV row
            csv_file.printf("%lu,%s,LOADCELL,",
                          lc_sample.timestamp_offset_us,
                          timestampToISO(abs_timestamp).c_str());
            csv_file.printf("%ld,%.6f,", lc_sample.raw_adc, load_kg);
            csv_file.printf(",,,,,\n");  // Empty IMU columns
            
            stats.loadcell_samples++;
            stats.bytes_read += sizeof(LoadcellSample);
            sample_count++;
            
            // Check if next is IMU sample (every 64th sample)
            if (sample_count % 64 == 0 && bin_file.available() >= (int)sizeof(IMUSample)) {
                if (bin_file.read((uint8_t*)&imu_sample, sizeof(imu_sample)) == sizeof(imu_sample)) {
                    abs_timestamp = header.start_timestamp_us + imu_sample.timestamp_offset_us;
                    
                    // Write CSV row
                    csv_file.printf("%lu,%s,IMU,",
                                  imu_sample.timestamp_offset_us,
                                  timestampToISO(abs_timestamp).c_str());
                    csv_file.printf(",,");  // Empty loadcell columns
                    csv_file.printf("%d,%d,%d,", imu_sample.accel_x, imu_sample.accel_y, imu_sample.accel_z);
                    csv_file.printf("%d,%d,%d\n", imu_sample.gyro_x, imu_sample.gyro_y, imu_sample.gyro_z);
                    
                    stats.imu_samples++;
                    stats.bytes_read += sizeof(IMUSample);
                }
            }
        }
        
        // Update progress
        if (progress_callback && file_size > 0) {
            int progress = (bin_file.position() * 100) / file_size;
            if (progress != last_progress) {
                progress_callback(progress);
                last_progress = progress;
            }
        }
    }
    
    stats.bytes_written = csv_file.size();
    stats.duration_ms = millis() - start_time;
    
    csv_file.close();
    bin_file.close();
    
    Serial.printf("CSV: Conversion complete\n");
    Serial.printf("CSV: %u loadcell samples, %u IMU samples\n",
                  stats.loadcell_samples, stats.imu_samples);
    Serial.printf("CSV: %u bytes read, %u bytes written in %u ms\n",
                  stats.bytes_read, stats.bytes_written, stats.duration_ms);
    Serial.printf("CSV: Output: %s\n", last_csv_path.c_str());
    
    return true;
}
