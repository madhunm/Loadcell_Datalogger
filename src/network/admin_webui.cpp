/**
 * @file admin_webui.cpp
 * @brief Admin WebUI HTTP Server Implementation (ESP-IDF Native)
 * 
 * Flash-optimized implementation using:
 * - ESP-IDF native esp_http_server (no external libraries)
 * - Manual JSON string building (no ArduinoJson)
 * - Minimal memory footprint
 */

// Set to 0 to disable verbose debug output
#define DEBUG_VERBOSE 0

#include "admin_webui.h"
#include "../app/app_mode.h"
#include "../pin_config.h"
#include "../drivers/status_led.h"
#include "../drivers/max11270.h"
#include "../drivers/max17048.h"
#include "../drivers/sd_manager.h"
#include "../drivers/lsm6dsv.h"
#include "../drivers/rx8900ce.h"
#include "../logging/logger_module.h"
#include "../logging/binary_format.h"
#include "../calibration/calibration_storage.h"
#include "../calibration/calibration_interp.h"
#include <esp_http_server.h>
#include <SD_MMC.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <SPIFFS.h>
#include <cmath>

namespace AdminWebUI {

namespace {
    httpd_handle_t server = nullptr;
    bool serverRunning = false;
    
    // Reusable response buffer (larger for SD card file lists)
    char jsonBuf[1024];
    
    // ========================================================================
    // JSON Building Helpers (manual, no ArduinoJson)
    // ========================================================================
    
    void sendJson(httpd_req_t* req, const char* json, int status = 200) {
        if (status != 200) {
            httpd_resp_set_status(req, status == 400 ? "400 Bad Request" :
                                       status == 401 ? "401 Unauthorized" :
                                       status == 403 ? "403 Forbidden" :
                                       status == 404 ? "404 Not Found" : "500 Error");
        }
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json, strlen(json));
    }
    
    void sendError(httpd_req_t* req, const char* message, int code = 400) {
        snprintf(jsonBuf, sizeof(jsonBuf), "{\"success\":false,\"error\":\"%s\"}", message);
        sendJson(req, jsonBuf, code);
    }
    
    void sendSuccess(httpd_req_t* req, const char* message = nullptr) {
        if (message) {
            snprintf(jsonBuf, sizeof(jsonBuf), "{\"success\":true,\"message\":\"%s\"}", message);
        } else {
            strcpy(jsonBuf, "{\"success\":true}");
        }
        sendJson(req, jsonBuf);
    }
    
    // ========================================================================
    // Simple JSON Parser (for POST bodies)
    // ========================================================================
    
    // Extract string value from JSON: {"key":"value"} -> value
    bool jsonGetString(const char* json, const char* key, char* out, size_t outLen) {
        char searchKey[64];
        snprintf(searchKey, sizeof(searchKey), "\"%s\":\"", key);
        
        const char* start = strstr(json, searchKey);
        if (!start) return false;
        
        start += strlen(searchKey);
        const char* end = strchr(start, '"');
        if (!end) return false;
        
        size_t len = end - start;
        if (len >= outLen) len = outLen - 1;
        strncpy(out, start, len);
        out[len] = '\0';
        return true;
    }
    
    // Extract integer value from JSON: {"key":123} -> 123
    bool jsonGetInt(const char* json, const char* key, int* out) {
        char searchKey[64];
        snprintf(searchKey, sizeof(searchKey), "\"%s\":", key);
        
        const char* start = strstr(json, searchKey);
        if (!start) return false;
        
        start += strlen(searchKey);
        // Skip whitespace
        while (*start == ' ' || *start == '\t') start++;
        
        *out = atoi(start);
        return true;
    }
    
    // ========================================================================
    // API Handlers
    // ========================================================================
    
    esp_err_t handleGetMode(httpd_req_t* req) {
        snprintf(jsonBuf, sizeof(jsonBuf), "{\"mode\":\"%s\"}", AppMode::getModeString());
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleGetStatus(httpd_req_t* req) {
        // Get SD card info
        bool sdPresent = SDManager::isMounted();
        uint64_t sdTotalMB = 0, sdUsedMB = 0;
        if (sdPresent) {
            SDManager::CardInfo info;
            if (SDManager::getCardInfo(&info)) {
                sdTotalMB = info.totalBytes / (1024 * 1024);
                sdUsedMB = info.usedBytes / (1024 * 1024);
            }
        }
        
        // Get ADC sample rate from logger config
        uint32_t adcSampleRateHz = Logger::getAdcRateHz();
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"mode\":\"%s\",\"wifi\":true,\"uptime_ms\":%lu,\"free_heap\":%u,"
            "\"adc_present\":%s,\"sd_present\":%s,\"sd_total_mb\":%llu,\"sd_used_mb\":%llu,"
            "\"adc_sample_rate_hz\":%lu,\"logging\":%s}",
            AppMode::getModeString(),
            millis(),
            ESP.getFreeHeap(),
            MAX11270::isPresent() ? "true" : "false",
            sdPresent ? "true" : "false",
            sdTotalMB,
            sdUsedMB,
            adcSampleRateHz,
            Logger::isRunning() ? "true" : "false"
        );
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleGetLive(httpd_req_t* req) {
        int32_t rawAdc = MAX11270::readSingle(50);
        MAX11270::Statistics stats = MAX11270::getStatistics();
        
        // Read IMU data
        float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
        LSM6DSV::ScaledData imuData;
        if (LSM6DSV::readScaled(&imuData)) {
            ax = imuData.accel[0];
            ay = imuData.accel[1];
            az = imuData.accel[2];
            gx = imuData.gyro[0];
            gy = imuData.gyro[1];
            gz = imuData.gyro[2];
        }
        
        if (rawAdc != INT32_MIN) {
            float uV = MAX11270::rawToMicrovolts(rawAdc);
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\"timestamp_ms\":%lu,\"raw_adc\":%ld,\"voltage_uV\":%.2f,\"load_kg\":%.4f,"
                "\"adc_samples\":%lu,\"adc_dropped\":%lu,"
                "\"accel_x\":%.3f,\"accel_y\":%.3f,\"accel_z\":%.3f,"
                "\"gyro_x\":%.1f,\"gyro_y\":%.1f,\"gyro_z\":%.1f}",
                millis(), (long)rawAdc, uV, uV / 10.0,
                (unsigned long)stats.samplesAcquired, (unsigned long)stats.samplesDropped,
                ax, ay, az, gx, gy, gz
            );
        } else {
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\"timestamp_ms\":%lu,\"raw_adc\":0,\"adc_error\":true,"
                "\"adc_samples\":%lu,\"adc_dropped\":%lu,"
                "\"accel_x\":%.3f,\"accel_y\":%.3f,\"accel_z\":%.3f,"
                "\"gyro_x\":%.1f,\"gyro_y\":%.1f,\"gyro_z\":%.1f}",
                millis(),
                (unsigned long)stats.samplesAcquired, (unsigned long)stats.samplesDropped,
                ax, ay, az, gx, gy, gz
            );
        }
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleGetBattery(httpd_req_t* req) {
        MAX17048::BatteryData batt;
        bool present = MAX17048::isPresent();
        
        if (present && MAX17048::getBatteryData(&batt)) {
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\"voltage_V\":%.3f,\"soc_percent\":%.1f,\"charge_rate_pct_hr\":%.2f,\"present\":true}",
                batt.voltage, batt.socPercent, batt.chargeRate
            );
        } else {
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\"voltage_V\":0,\"soc_percent\":0,\"charge_rate_pct_hr\":0,\"present\":%s}",
                present ? "true" : "false"
            );
        }
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleGetLed(httpd_req_t* req) {
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"state_index\":%d,\"state_count\":%d,\"state_name\":\"%s\",\"cycling\":%s}",
            StatusLED::getTestStateIndex(),
            StatusLED::getTestStateCount(),
            StatusLED::getTestStateName(),
            StatusLED::isTestCycling() ? "true" : "false"
        );
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleGetSessionSummary(httpd_req_t* req) {
        Logger::SessionSummary summary = Logger::getSessionSummary();
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"valid\":%s,"
            "\"peak_load_n\":%.2f,\"peak_load_time_s\":%.3f,"
            "\"peak_decel_g\":%.2f,\"peak_decel_time_s\":%.3f,"
            "\"total_adc_samples\":%llu,\"total_imu_samples\":%llu,"
            "\"duration_s\":%.2f,\"dropped_samples\":%lu,"
            "\"is_running\":%s}",
            summary.valid ? "true" : "false",
            summary.peakLoadN, summary.peakLoadTimeMs / 1000.0f,
            summary.peakDecelG, summary.peakDecelTimeMs / 1000.0f,
            summary.totalAdcSamples, summary.totalImuSamples,
            summary.durationMs / 1000.0f, summary.droppedSamples,
            Logger::isRunning() ? "true" : "false"
        );
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleGetRecoveryStatus(httpd_req_t* req) {
        bool hasRecovery = Logger::hasRecoveryData();
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"has_recovery\":%s}",
            hasRecovery ? "true" : "false"
        );
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handlePostRecoverSession(httpd_req_t* req) {
        if (Logger::recoverSession()) {
            sendSuccess(req, "Session recovered");
        } else {
            sendError(req, "No session to recover", 404);
        }
        return ESP_OK;
    }
    
    esp_err_t handlePostClearRecovery(httpd_req_t* req) {
        Logger::clearRecoveryData();
        sendSuccess(req, "Recovery data cleared");
        return ESP_OK;
    }
    
    esp_err_t handleLedNext(httpd_req_t* req) {
        if (!AppMode::canFactoryTest()) {
            sendError(req, "Factory mode required", 403);
            return ESP_OK;
        }
        StatusLED::nextTestState();
        return handleGetLed(req);
    }
    
    esp_err_t handleLedCycleStart(httpd_req_t* req) {
        if (!AppMode::canFactoryTest()) {
            sendError(req, "Factory mode required", 403);
            return ESP_OK;
        }
        StatusLED::startTestCycle(1500);
        strcpy(jsonBuf, "{\"success\":true,\"cycling\":true}");
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleLedCycleStop(httpd_req_t* req) {
        StatusLED::stopTestCycle();
        strcpy(jsonBuf, "{\"success\":true,\"cycling\":false}");
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleLoggingStart(httpd_req_t* req) {
        Serial.println("[WebUI] handleLoggingStart() called");
        Serial.printf("[WebUI] Current mode: %s, canLog: %s\n", 
                      AppMode::getModeString(), 
                      AppMode::canLog() ? "YES" : "NO");
        
        if (!AppMode::canLog()) {
            Serial.println("[WebUI] ERROR: Logging not allowed in current mode");
            sendError(req, "Logging not allowed", 403);
            return ESP_OK;
        }
        
        if (Logger::isRunning()) {
            Serial.println("[WebUI] ERROR: Already logging");
            sendError(req, "Already logging", 400);
            return ESP_OK;
        }
        
        Serial.println("[WebUI] Calling Logger::start()...");
        uint32_t startMs = millis();
        
        if (!Logger::start()) {
            Serial.printf("[WebUI] ERROR: Logger::start() failed after %lu ms\n", millis() - startMs);
            sendError(req, "Failed to start logger - check SD card", 500);
            return ESP_OK;
        }
        
        Serial.printf("[WebUI] Logger::start() succeeded in %lu ms\n", millis() - startMs);
        
        StatusLED::setState(StatusLED::State::Logging);
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"success\":true,\"message\":\"Logging started\",\"file\":\"%s\"}",
            Logger::getCurrentFilePath()
        );
        sendJson(req, jsonBuf);
        Serial.println("[WebUI] Logging start response sent");
        return ESP_OK;
    }
    
    esp_err_t handleLoggingStop(httpd_req_t* req) {
        if (Logger::isRunning()) {
            Logger::stop();
        }
        
        switch (AppMode::getMode()) {
            case AppMode::Mode::User:
                StatusLED::setState(StatusLED::State::IdleUser);
                break;
            case AppMode::Mode::FieldAdmin:
                StatusLED::setState(StatusLED::State::IdleAdmin);
                break;
            default:
                StatusLED::setState(StatusLED::State::Ready);
                break;
        }
        sendSuccess(req, "Logging stopped");
        return ESP_OK;
    }
    
    esp_err_t handleTestAdc(httpd_req_t* req) {
        if (!AppMode::canFactoryTest()) {
            sendError(req, "Factory mode required", 403);
            return ESP_OK;
        }
        
        StatusLED::setState(StatusLED::State::FactoryTesting);
        
        bool passed = false;
        int32_t reading = 0;
        float voltage = 0;
        
        if (MAX11270::isPresent()) {
            reading = MAX11270::readSingle(100);
            if (reading != INT32_MIN) {
                voltage = MAX11270::rawToMicrovolts(reading);
                passed = true;
            }
        }
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"sensor\":\"adc\",\"passed\":%s,\"raw_value\":%ld,\"voltage_uV\":%.2f}",
            passed ? "true" : "false", (long)reading, voltage
        );
        
        StatusLED::setState(StatusLED::State::IdleFactory);
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleTestLed(httpd_req_t* req) {
        if (!AppMode::canFactoryTest()) {
            sendError(req, "Factory mode required", 403);
            return ESP_OK;
        }
        strcpy(jsonBuf, "{\"sensor\":\"led\",\"passed\":true}");
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleTestIMU(httpd_req_t* req) {
        if (!AppMode::canFactoryTest()) {
            sendError(req, "Factory mode required", 403);
            return ESP_OK;
        }
        
        StatusLED::setState(StatusLED::State::FactoryTesting);
        
        bool passed = false;
        const char* message = "Not detected";
        float ax = 0, ay = 0, az = 0;
        
        if (LSM6DSV::isPresent()) {
            LSM6DSV::ScaledData data;
            if (LSM6DSV::readScaled(&data)) {
                ax = data.accel[0];
                ay = data.accel[1];
                az = data.accel[2];
                // Check if Z-axis shows roughly 1g (gravity)
                float totalG = sqrt(ax*ax + ay*ay + az*az);
                if (totalG > 0.8f && totalG < 1.2f) {
                    passed = true;
                    message = "OK - gravity detected";
                } else {
                    message = "Unexpected acceleration values";
                }
            } else {
                message = "Read failed";
            }
        }
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"sensor\":\"imu\",\"passed\":%s,\"message\":\"%s\","
            "\"accel_x\":%.3f,\"accel_y\":%.3f,\"accel_z\":%.3f}",
            passed ? "true" : "false", message, ax, ay, az
        );
        
        StatusLED::setState(StatusLED::State::IdleFactory);
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleTestRTC(httpd_req_t* req) {
        if (!AppMode::canFactoryTest()) {
            sendError(req, "Factory mode required", 403);
            return ESP_OK;
        }
        
        StatusLED::setState(StatusLED::State::FactoryTesting);
        
        bool passed = false;
        const char* message = "Not detected";
        time_t epoch = 0;
        float temp = 0;
        
        if (RX8900CE::isPresent()) {
            epoch = RX8900CE::getEpoch();
            temp = RX8900CE::getTemperature();
            
            // Basic validation: epoch should be reasonable (after 2020)
            if (epoch > 1577836800) {  // Jan 1, 2020
                passed = true;
                message = "OK";
            } else {
                message = "Invalid time - needs sync";
            }
        }
        
        char timeBuf[24];
        RX8900CE::formatTime(epoch, timeBuf, sizeof(timeBuf));
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"sensor\":\"rtc\",\"passed\":%s,\"message\":\"%s\","
            "\"time\":\"%s\",\"epoch\":%lu,\"temp_C\":%.1f}",
            passed ? "true" : "false", message, timeBuf, (unsigned long)epoch, temp
        );
        
        StatusLED::setState(StatusLED::State::IdleFactory);
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleTestSD(httpd_req_t* req) {
        if (!AppMode::canFactoryTest()) {
            sendError(req, "Factory mode required", 403);
            return ESP_OK;
        }
        
        StatusLED::setState(StatusLED::State::FactoryTesting);
        
        bool passed = false;
        const char* message = "Not detected";
        uint64_t totalMB = 0, freeMB = 0;
        
        if (SDManager::isMounted()) {
            SDManager::CardInfo info;
            if (SDManager::getCardInfo(&info)) {
                totalMB = info.totalBytes / (1024 * 1024);
                freeMB = (info.totalBytes - info.usedBytes) / (1024 * 1024);
                passed = true;
                message = "OK";
            } else {
                message = "Card info failed";
            }
        } else if (SDManager::isCardPresent()) {
            message = "Card present but not mounted";
        }
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"sensor\":\"sd\",\"passed\":%s,\"message\":\"%s\","
            "\"total_mb\":%llu,\"free_mb\":%llu,\"type\":\"%s\"}",
            passed ? "true" : "false", message, totalMB, freeMB,
            SDManager::getCardTypeString()
        );
        
        StatusLED::setState(StatusLED::State::IdleFactory);
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleTestBattery(httpd_req_t* req) {
        if (!AppMode::canFactoryTest()) {
            sendError(req, "Factory mode required", 403);
            return ESP_OK;
        }
        
        StatusLED::setState(StatusLED::State::FactoryTesting);
        
        bool passed = false;
        const char* message = "Not detected";
        float voltage = 0, soc = 0;
        
        if (MAX17048::isPresent()) {
            MAX17048::BatteryData batt;
            if (MAX17048::getBatteryData(&batt)) {
                voltage = batt.voltage;
                soc = batt.socPercent;
                
                // Basic validation: voltage should be reasonable (2.5V - 4.5V for LiPo)
                if (voltage > 2.5f && voltage < 4.5f) {
                    passed = true;
                    message = "OK";
                } else {
                    message = "Voltage out of range";
                }
            } else {
                message = "Read failed";
            }
        }
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"sensor\":\"battery\",\"passed\":%s,\"message\":\"%s\","
            "\"voltage_V\":%.3f,\"soc_percent\":%.1f}",
            passed ? "true" : "false", message, voltage, soc
        );
        
        StatusLED::setState(StatusLED::State::IdleFactory);
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleGetSDCard(httpd_req_t* req) {
        bool present = SDManager::isMounted();
        uint64_t totalMB = 0, freeMB = 0, usedMB = 0;
        
        if (present) {
            SDManager::CardInfo info;
            if (SDManager::getCardInfo(&info)) {
                totalMB = info.totalBytes / (1024 * 1024);
                usedMB = info.usedBytes / (1024 * 1024);
                freeMB = totalMB - usedMB;
            }
        }
        
        // Note: File listing would require more complex code and larger buffer
        // For now, just return card stats
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"present\":%s,\"total_mb\":%llu,\"free_mb\":%llu,\"used_mb\":%llu,\"files\":[]}",
            present ? "true" : "false", totalMB, freeMB, usedMB
        );
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleGetSDHealth(httpd_req_t* req) {
        SDManager::Health health = SDManager::getHealth();
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"mounted\":%s,\"total_mb\":%llu,\"used_mb\":%llu,\"free_mb\":%llu,"
            "\"write_count\":%lu,\"avg_latency_ms\":%.2f,\"max_latency_ms\":%.2f,"
            "\"warning\":%s,\"warning_msg\":\"%s\"}",
            health.mounted ? "true" : "false",
            health.totalBytes / (1024 * 1024),
            health.usedBytes / (1024 * 1024),
            health.freeBytes / (1024 * 1024),
            health.writeCount,
            health.avgWriteLatencyMs,
            health.maxWriteLatencyMs,
            health.healthWarning ? "true" : "false",
            health.warningMessage ? health.warningMessage : ""
        );
        
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handleValidateFile(httpd_req_t* req) {
        // Extract filename from URL query string
        char filepath[128] = "/data/";
        char query[64] = {0};
        
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            char filename[64];
            if (httpd_query_key_value(query, "file", filename, sizeof(filename)) == ESP_OK) {
                strncat(filepath, filename, sizeof(filepath) - strlen(filepath) - 1);
            }
        }
        
        if (strlen(filepath) <= 6) {
            sendError(req, "Missing 'file' parameter", 400);
            return ESP_OK;
        }
        
        // Open and validate file
        File file = SD_MMC.open(filepath, FILE_READ);
        if (!file) {
            sendError(req, "File not found", 404);
            return ESP_OK;
        }
        
        // Read header
        BinaryFormat::FileHeader header;
        if (file.read((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
            file.close();
            sendError(req, "Failed to read header", 500);
            return ESP_OK;
        }
        
        bool headerValid = header.isValid();
        
        // Try to read footer from end
        bool footerPresent = false;
        bool footerValid = false;
        BinaryFormat::FileFooter footer;
        
        size_t fileSize = file.size();
        if (fileSize >= sizeof(header) + sizeof(footer)) {
            file.seek(fileSize - sizeof(footer));
            if (file.read((uint8_t*)&footer, sizeof(footer)) == sizeof(footer)) {
                footerPresent = true;
                footerValid = footer.isValid();
            }
        }
        
        // Count records and check for gaps (simplified - just counts)
        file.seek(sizeof(header));
        uint32_t adcCount = 0;
        uint32_t imuCount = 0;
        uint32_t expectedSeq = 0;
        uint32_t gaps = 0;
        
        // Calculate expected IMU decimation
        uint32_t imuDecim = header.imuSampleRateHz > 0 ? 
                           (header.adcSampleRateHz / header.imuSampleRateHz) : 0;
        
        BinaryFormat::ADCRecord adc;
        while (file.available() >= (int)sizeof(adc)) {
            // Check for footer magic
            size_t pos = file.position();
            if (fileSize - pos <= sizeof(footer) + 10) break;
            
            if (file.read((uint8_t*)&adc, sizeof(adc)) != sizeof(adc)) break;
            
            // Check for end marker
            if (*((uint8_t*)&adc) == 0xFF) break;
            
            adcCount++;
            
            // Check sequence
            if (adc.sequenceNum != expectedSeq) {
                gaps++;
            }
            expectedSeq = adc.sequenceNum + 1;
            
            // Skip IMU record if interleaved
            if (imuDecim > 0 && adcCount % imuDecim == 0) {
                BinaryFormat::IMURecord imu;
                if (file.read((uint8_t*)&imu, sizeof(imu)) == sizeof(imu)) {
                    imuCount++;
                }
            }
            
            // Limit to avoid long validation times
            if (adcCount > 1000000) break;
        }
        
        file.close();
        
        // Build response
        bool isValid = headerValid && (!footerPresent || footerValid) && gaps == 0;
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"valid\":%s,\"header_valid\":%s,\"footer_present\":%s,\"footer_valid\":%s,"
            "\"adc_count\":%lu,\"imu_count\":%lu,\"gaps\":%lu,"
            "\"expected_adc\":%llu,\"expected_imu\":%llu,\"dropped\":%lu}",
            isValid ? "true" : "false",
            headerValid ? "true" : "false",
            footerPresent ? "true" : "false",
            footerValid ? "true" : "false",
            adcCount, imuCount, gaps,
            footerValid ? footer.totalAdcSamples : 0ULL,
            footerValid ? footer.totalImuSamples : 0ULL,
            footerValid ? footer.droppedSamples : 0UL
        );
        
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    // Server-Sent Events (SSE) stream for real-time sensor data
    esp_err_t handleStream(httpd_req_t* req) {
        // Set SSE headers
        httpd_resp_set_type(req, "text/event-stream");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        
        char sseBuf[448];
        
#if DEBUG_VERBOSE
        Serial.println("[SSE] Client connected to stream");
#endif
        
        // Get ADC sample rate (doesn't change during session)
        uint32_t adcSampleRateHz = Logger::getAdcRateHz();
        
        while (true) {
            // Read ADC
            int32_t rawAdc = MAX11270::readSingle(10);  // Fast read
            float uV = (rawAdc != INT32_MIN) ? MAX11270::rawToMicrovolts(rawAdc) : 0;
            
            // Read IMU
            float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
            LSM6DSV::ScaledData imuData;
            if (LSM6DSV::readScaled(&imuData)) {
                ax = imuData.accel[0];
                ay = imuData.accel[1];
                az = imuData.accel[2];
                gx = imuData.gyro[0];
                gy = imuData.gyro[1];
                gz = imuData.gyro[2];
            }
            
            // Get logging integrity data if running
            bool logging = Logger::isRunning();
            float bufferFill = 0;
            uint32_t latencyUs = 0;
            uint32_t drops = 0;
            
            if (logging) {
                Logger::Status status = Logger::getStatus();
                bufferFill = status.fillPercent;
                latencyUs = status.writeStats.avgUs;
                drops = status.droppedSamples;
            }
            
            // Format SSE message with integrity data and sample rate
            int len = snprintf(sseBuf, sizeof(sseBuf),
                "data: {\"adc\":%ld,\"uV\":%.1f,\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
                "\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,\"t\":%lu,"
                "\"logging\":%s,\"buf_pct\":%.1f,\"latency_us\":%lu,\"drops\":%lu,"
                "\"sample_rate_hz\":%lu}\n\n",
                (long)rawAdc, uV, ax, ay, az, gx, gy, gz, millis(),
                logging ? "true" : "false", bufferFill, latencyUs, drops,
                adcSampleRateHz);
            
            // Send chunk - if this fails, client disconnected
            if (httpd_resp_send_chunk(req, sseBuf, len) != ESP_OK) {
#if DEBUG_VERBOSE
                Serial.println("[SSE] Client disconnected");
#endif
                break;
            }
            
            // ~20Hz update rate (adjustable)
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        
        // End chunked response
        httpd_resp_send_chunk(req, nullptr, 0);
        return ESP_OK;
    }
    
    esp_err_t handlePostMode(httpd_req_t* req) {
        // Read POST body
        char body[256];
        int len = httpd_req_recv(req, body, sizeof(body) - 1);
        if (len <= 0) {
            sendError(req, "No body");
            return ESP_OK;
        }
        body[len] = '\0';
        
        // Parse JSON
        char modeStr[16] = {0};
        char password[32] = {0};
        
        if (!jsonGetString(body, "mode", modeStr, sizeof(modeStr))) {
            sendError(req, "Missing mode");
            return ESP_OK;
        }
        jsonGetString(body, "password", password, sizeof(password));
        
        // Determine mode
        AppMode::Mode newMode;
        if (strcmp(modeStr, "user") == 0) {
            newMode = AppMode::Mode::User;
        } else if (strcmp(modeStr, "admin") == 0) {
            newMode = AppMode::Mode::FieldAdmin;
        } else if (strcmp(modeStr, "factory") == 0) {
            newMode = AppMode::Mode::Factory;
        } else {
            sendError(req, "Invalid mode");
            return ESP_OK;
        }
        
        // Try to set mode
        if (AppMode::setMode(newMode, password)) {
            switch (newMode) {
                case AppMode::Mode::User:
                    StatusLED::setState(StatusLED::State::IdleUser);
                    break;
                case AppMode::Mode::FieldAdmin:
                    StatusLED::setState(StatusLED::State::IdleAdmin);
                    break;
                case AppMode::Mode::Factory:
                    StatusLED::setState(StatusLED::State::IdleFactory);
                    break;
            }
            snprintf(jsonBuf, sizeof(jsonBuf), "{\"success\":true,\"mode\":\"%s\"}", 
                     AppMode::getModeString());
            sendJson(req, jsonBuf);
        } else {
            sendError(req, "Invalid password", 401);
        }
        return ESP_OK;
    }
    
    esp_err_t handlePostLed(httpd_req_t* req) {
        if (!AppMode::canFactoryTest()) {
            sendError(req, "Factory mode required", 403);
            return ESP_OK;
        }
        
        // Read POST body
        char body[256];
        int len = httpd_req_recv(req, body, sizeof(body) - 1);
        if (len <= 0) {
            sendError(req, "No body");
            return ESP_OK;
        }
        body[len] = '\0';
        
        // Parse color
        char colorStr[16] = {0};
        if (!jsonGetString(body, "color", colorStr, sizeof(colorStr))) {
            sendError(req, "Missing color");
            return ESP_OK;
        }
        
        StatusLED::Color color = StatusLED::Colors::Off;
        if (strcmp(colorStr, "red") == 0) color = StatusLED::Colors::Red;
        else if (strcmp(colorStr, "green") == 0) color = StatusLED::Colors::Green;
        else if (strcmp(colorStr, "blue") == 0) color = StatusLED::Colors::Blue;
        else if (strcmp(colorStr, "cyan") == 0) color = StatusLED::Colors::Cyan;
        else if (strcmp(colorStr, "orange") == 0) color = StatusLED::Colors::Orange;
        else if (strcmp(colorStr, "magenta") == 0) color = StatusLED::Colors::Magenta;
        
        // Parse pattern (optional)
        StatusLED::Pattern pattern = StatusLED::Pattern::Steady;
        char patternStr[16] = {0};
        if (jsonGetString(body, "pattern", patternStr, sizeof(patternStr))) {
            if (strcmp(patternStr, "pulse") == 0) pattern = StatusLED::Pattern::Pulse;
            else if (strcmp(patternStr, "fast_blink") == 0) pattern = StatusLED::Pattern::FastBlink;
            else if (strcmp(patternStr, "blink_code") == 0) pattern = StatusLED::Pattern::BlinkCode;
        }
        
        // Parse blink count (optional)
        int blinkCount = 1;
        jsonGetInt(body, "blink_count", &blinkCount);
        
        StatusLED::setTestMode(color, pattern, (uint8_t)blinkCount);
        sendSuccess(req);
        return ESP_OK;
    }
    
    esp_err_t handleOptions(httpd_req_t* req) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    
    // OTA Update Handler
    esp_err_t handleOtaUpload(httpd_req_t* req) {
        if (!AppMode::canFactoryTest() && AppMode::getMode() != AppMode::Mode::FieldAdmin) {
            sendError(req, "Admin or Factory mode required", 403);
            return ESP_OK;
        }
        
        char buf[1024];
        int received;
        int remaining = req->content_len;
        bool isFirstChunk = true;
        esp_ota_handle_t otaHandle = 0;
        const esp_partition_t* updatePartition = nullptr;
        esp_err_t err;
        
        Serial.printf("[OTA] Starting update, size: %d bytes\n", remaining);
        
        // Get the next OTA partition
        updatePartition = esp_ota_get_next_update_partition(nullptr);
        if (!updatePartition) {
            sendError(req, "No OTA partition found", 500);
            return ESP_OK;
        }
        
        Serial.printf("[OTA] Writing to partition: %s\n", updatePartition->label);
        
        while (remaining > 0) {
            int readLen = remaining < (int)sizeof(buf) ? remaining : sizeof(buf);
            received = httpd_req_recv(req, buf, readLen);
            
            if (received <= 0) {
                if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                    continue;  // Retry on timeout
                }
                Serial.println("[OTA] Receive error");
                if (otaHandle) {
                    esp_ota_abort(otaHandle);
                }
                sendError(req, "Receive failed", 500);
                return ESP_OK;
            }
            
            if (isFirstChunk) {
                isFirstChunk = false;
                
                // Validate firmware header (check for ESP32 magic)
                if (received < 4 || buf[0] != (char)0xE9) {
                    sendError(req, "Invalid firmware format", 400);
                    return ESP_OK;
                }
                
                err = esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle);
                if (err != ESP_OK) {
                    Serial.printf("[OTA] Begin failed: %s\n", esp_err_to_name(err));
                    sendError(req, "OTA begin failed", 500);
                    return ESP_OK;
                }
            }
            
            err = esp_ota_write(otaHandle, buf, received);
            if (err != ESP_OK) {
                Serial.printf("[OTA] Write failed: %s\n", esp_err_to_name(err));
                esp_ota_abort(otaHandle);
                sendError(req, "OTA write failed", 500);
                return ESP_OK;
            }
            
            remaining -= received;
            
            // Progress feedback every 64KB
            static int lastProgress = 0;
            int progress = ((req->content_len - remaining) * 100) / req->content_len;
            if (progress / 10 != lastProgress / 10) {
                Serial.printf("[OTA] Progress: %d%%\n", progress);
                lastProgress = progress;
            }
        }
        
        err = esp_ota_end(otaHandle);
        if (err != ESP_OK) {
            Serial.printf("[OTA] End failed: %s\n", esp_err_to_name(err));
            sendError(req, "OTA verification failed", 500);
            return ESP_OK;
        }
        
        err = esp_ota_set_boot_partition(updatePartition);
        if (err != ESP_OK) {
            Serial.printf("[OTA] Set boot partition failed: %s\n", esp_err_to_name(err));
            sendError(req, "Set boot partition failed", 500);
            return ESP_OK;
        }
        
        Serial.println("[OTA] Update successful! Restarting...");
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"success\":true,\"message\":\"Update successful. Restarting in 2 seconds...\"}");
        sendJson(req, jsonBuf);
        
        // Delay restart to allow response to be sent
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        
        return ESP_OK;
    }
    
    // ========================================================================
    // ADC Diagnostic Endpoint
    // ========================================================================
    
    esp_err_t handleDiagAdc(httpd_req_t* req) {
        // Read ADC registers for diagnostics
        uint32_t stat1 = MAX11270::readRegister(MAX11270::Register::STAT1);
        uint32_t ctrl1 = MAX11270::readRegister(MAX11270::Register::CTRL1);
        uint32_t ctrl2 = MAX11270::readRegister(MAX11270::Register::CTRL2);
        uint32_t ctrl3 = MAX11270::readRegister(MAX11270::Register::CTRL3);
        
        // Read GPIO states
        int rdyb = digitalRead(PIN_ADC_RDYB);
        int sync = digitalRead(PIN_ADC_SYNC);
        int rstb = digitalRead(PIN_ADC_RSTB);
        
        // Take a few readings
        int32_t readings[3];
        float voltages[3];
        for (int i = 0; i < 3; i++) {
            readings[i] = MAX11270::readSingle(50);
            voltages[i] = (readings[i] != INT32_MIN) ? MAX11270::rawToMicrovolts(readings[i]) : 0;
            delay(10);
        }
        
        // Get current config
        uint8_t gain = MAX11270::gainToMultiplier(MAX11270::getGain());
        uint32_t rate = MAX11270::rateToHz(MAX11270::getSampleRate());
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"registers\":{\"STAT1\":\"0x%02X\",\"CTRL1\":\"0x%02X\",\"CTRL2\":\"0x%02X\",\"CTRL3\":\"0x%02X\"},"
            "\"gpio\":{\"RDYB\":%d,\"SYNC\":%d,\"RSTB\":%d},"
            "\"config\":{\"gain\":%d,\"rate_hz\":%lu},"
            "\"readings\":[%ld,%ld,%ld],"
            "\"voltages_uV\":[%.1f,%.1f,%.1f],"
            "\"adc_present\":%s}",
            stat1, ctrl1, ctrl2, ctrl3,
            rdyb, sync, rstb,
            gain, rate,
            (long)readings[0], (long)readings[1], (long)readings[2],
            voltages[0], voltages[1], voltages[2],
            MAX11270::isPresent() ? "true" : "false"
        );
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    // ========================================================================
    // Calibration Config Endpoints
    // ========================================================================
    
    // Helper to extract float from JSON
    bool jsonGetFloat(const char* json, const char* key, float* out) {
        char searchKey[64];
        snprintf(searchKey, sizeof(searchKey), "\"%s\":", key);
        
        const char* start = strstr(json, searchKey);
        if (!start) return false;
        
        start += strlen(searchKey);
        while (*start == ' ' || *start == '\t') start++;
        
        *out = atof(start);
        return true;
    }
    
    esp_err_t handleGetConfig(httpd_req_t* req) {
        Calibration::LoadcellCalibration cal;
        bool hasActive = CalibrationStorage::loadActive(&cal);
        
        if (!hasActive) {
            // Return empty config
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\"loaded\":false,\"loadcell_id\":\"\",\"loadcell_model\":\"\","
                "\"loadcell_serial\":\"\",\"capacity_kg\":0,\"excitation_V\":3.3,"
                "\"sensitivity_mVV\":0,\"zero_balance_uV\":0,\"calibration_points\":[]}"
            );
            sendJson(req, jsonBuf);
            return ESP_OK;
        }
        
        // Build calibration points array
        char pointsBuf[512] = "[";
        for (uint8_t i = 0; i < cal.numPoints; i++) {
            char pointStr[48];
            snprintf(pointStr, sizeof(pointStr), 
                "%s{\"load_kg\":%.1f,\"output_uV\":%.1f}",
                i > 0 ? "," : "",
                cal.points[i].load_kg,
                cal.points[i].output_uV);
            strncat(pointsBuf, pointStr, sizeof(pointsBuf) - strlen(pointsBuf) - 1);
        }
        strcat(pointsBuf, "]");
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"loaded\":true,\"loadcell_id\":\"%s\",\"loadcell_model\":\"%s\","
            "\"loadcell_serial\":\"%s\",\"capacity_kg\":%.1f,\"excitation_V\":%.2f,"
            "\"sensitivity_mVV\":%.4f,\"zero_balance_uV\":%.1f,\"calibration_points\":%s}",
            cal.id, cal.model, cal.serial,
            cal.capacity_kg, cal.excitation_V,
            cal.sensitivity_mVV, cal.zeroBalance_uV,
            pointsBuf
        );
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    esp_err_t handlePostConfig(httpd_req_t* req) {
        // Read POST body (larger buffer for calibration data)
        static char configBody[2048];
        int len = httpd_req_recv(req, configBody, sizeof(configBody) - 1);
        if (len <= 0) {
            sendError(req, "No body");
            return ESP_OK;
        }
        configBody[len] = '\0';
        
        // Parse JSON into calibration structure
        Calibration::LoadcellCalibration cal;
        cal.init();
        
        // Extract basic fields
        jsonGetString(configBody, "loadcell_id", cal.id, sizeof(cal.id));
        jsonGetString(configBody, "loadcell_model", cal.model, sizeof(cal.model));
        jsonGetString(configBody, "loadcell_serial", cal.serial, sizeof(cal.serial));
        jsonGetFloat(configBody, "capacity_kg", &cal.capacity_kg);
        jsonGetFloat(configBody, "excitation_V", &cal.excitation_V);
        jsonGetFloat(configBody, "sensitivity_mVV", &cal.sensitivity_mVV);
        jsonGetFloat(configBody, "zero_balance_uV", &cal.zeroBalance_uV);
        
        // Generate ID if not provided
        if (cal.id[0] == '\0' && cal.model[0] != '\0' && cal.serial[0] != '\0') {
            cal.generateId();
        }
        
        // Parse calibration_points array
        const char* pointsStart = strstr(configBody, "\"calibration_points\":");
        if (pointsStart) {
            pointsStart = strchr(pointsStart, '[');
            if (pointsStart) {
                pointsStart++; // Skip '['
                
                // Parse each point: {"load_kg":X,"output_uV":Y}
                while (*pointsStart && cal.numPoints < Calibration::MAX_CALIBRATION_POINTS) {
                    // Find next object
                    const char* objStart = strchr(pointsStart, '{');
                    if (!objStart) break;
                    
                    const char* objEnd = strchr(objStart, '}');
                    if (!objEnd) break;
                    
                    // Extract load_kg and output_uV from this object
                    float load = 0, output = 0;
                    
                    // Simple extraction for load_kg
                    const char* loadKey = strstr(objStart, "\"load_kg\":");
                    if (loadKey && loadKey < objEnd) {
                        load = atof(loadKey + 10);
                    }
                    
                    // Simple extraction for output_uV
                    const char* outputKey = strstr(objStart, "\"output_uV\":");
                    if (outputKey && outputKey < objEnd) {
                        output = atof(outputKey + 12);
                    }
                    
                    cal.addPoint(load, output);
                    pointsStart = objEnd + 1;
                }
            }
        }
        
        // Set timestamps
        cal.lastModified = millis() / 1000;
        
        // Validate
        if (!cal.isValid()) {
            sendError(req, "Invalid calibration: need ID, capacity, and at least 2 points");
            return ESP_OK;
        }
        
        // Sort points by output voltage
        cal.sortPoints();
        
        // Save to NVS
        if (!CalibrationStorage::save(cal)) {
            sendError(req, "Failed to save calibration to NVS", 500);
            return ESP_OK;
        }
        
        // Set as active
        CalibrationStorage::setActive(cal.id);
        
        // Reload interpolation module
        CalibrationInterp::reload();
        
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"success\":true,\"message\":\"Calibration saved\",\"id\":\"%s\",\"points\":%d}",
            cal.id, cal.numPoints);
        sendJson(req, jsonBuf);
        return ESP_OK;
    }
    
    // ========================================================================
    // Static File Server
    // ========================================================================
    
    const char* getContentType(const char* path) {
        if (strstr(path, ".html")) return "text/html";
        if (strstr(path, ".css")) return "text/css";
        if (strstr(path, ".js")) return "application/javascript";
        if (strstr(path, ".json")) return "application/json";
        if (strstr(path, ".png")) return "image/png";
        if (strstr(path, ".ico")) return "image/x-icon";
        return "text/plain";
    }
    
    esp_err_t handleStaticFile(httpd_req_t* req) {
        String uri = req->uri;
        
        // Default to index.html
        if (uri == "/" || uri.isEmpty()) {
            uri = "/index.html";
        }
        
        // Check if file exists in SPIFFS
        if (!SPIFFS.exists(uri)) {
            // Try adding .html
            if (!uri.endsWith(".html") && SPIFFS.exists(uri + ".html")) {
                uri += ".html";
            } else {
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }
        
        File file = SPIFFS.open(uri, "r");
        if (!file) {
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        
        // Set content type
        httpd_resp_set_type(req, getContentType(uri.c_str()));
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        
        // Stream file in chunks
        char buf[512];
        size_t bytesRead;
        while ((bytesRead = file.read((uint8_t*)buf, sizeof(buf))) > 0) {
            if (httpd_resp_send_chunk(req, buf, bytesRead) != ESP_OK) {
                file.close();
                return ESP_FAIL;
            }
        }
        
        // End chunked response
        httpd_resp_send_chunk(req, nullptr, 0);
        file.close();
        return ESP_OK;
    }

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

bool init() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[WebUI] SPIFFS mount failed");
        return false;
    }
    
#if DEBUG_VERBOSE
    // List files in SPIFFS for debugging
    Serial.println("[WebUI] SPIFFS mounted. Files:");
    File root = SPIFFS.open("/");
    if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
            file = root.openNextFile();
        }
    }
    
    // Check if index.html exists
    if (SPIFFS.exists("/index.html")) {
        Serial.println("[WebUI] index.html found");
    } else {
        Serial.println("[WebUI] WARNING: index.html NOT found!");
    }
#endif
    
    return true;
}

bool beginServer() {
    if (serverRunning) return true;
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 35;  // Increased for all endpoints
    config.stack_size = 8192;
    
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        Serial.printf("[WebUI] Server start failed: %d\n", ret);
        return false;
    }
    
    // ---- GET Routes ----
    httpd_uri_t get_mode = { .uri = "/api/mode", .method = HTTP_GET, 
        .handler = handleGetMode, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_mode);
    
    httpd_uri_t get_status = { .uri = "/api/status", .method = HTTP_GET,
        .handler = handleGetStatus, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_status);
    
    httpd_uri_t get_live = { .uri = "/api/live", .method = HTTP_GET,
        .handler = handleGetLive, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_live);
    
    httpd_uri_t get_battery = { .uri = "/api/battery", .method = HTTP_GET,
        .handler = handleGetBattery, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_battery);
    
    httpd_uri_t get_led = { .uri = "/api/led", .method = HTTP_GET,
        .handler = handleGetLed, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_led);
    
    httpd_uri_t get_sdcard = { .uri = "/api/sdcard", .method = HTTP_GET,
        .handler = handleGetSDCard, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_sdcard);
    
    httpd_uri_t get_stream = { .uri = "/api/stream", .method = HTTP_GET,
        .handler = handleStream, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_stream);
    
    httpd_uri_t get_session_summary = { .uri = "/api/session/summary", .method = HTTP_GET,
        .handler = handleGetSessionSummary, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_session_summary);
    
    httpd_uri_t get_recovery_status = { .uri = "/api/recovery/status", .method = HTTP_GET,
        .handler = handleGetRecoveryStatus, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_recovery_status);
    
    httpd_uri_t get_validate = { .uri = "/api/validate", .method = HTTP_GET,
        .handler = handleValidateFile, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_validate);
    
    httpd_uri_t get_sd_health = { .uri = "/api/sd/health", .method = HTTP_GET,
        .handler = handleGetSDHealth, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_sd_health);
    
    httpd_uri_t get_diag_adc = { .uri = "/api/diag/adc", .method = HTTP_GET,
        .handler = handleDiagAdc, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_diag_adc);
    
    httpd_uri_t get_config = { .uri = "/api/config", .method = HTTP_GET,
        .handler = handleGetConfig, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_config);
    
    // ---- POST Routes ----
    httpd_uri_t post_mode = { .uri = "/api/mode", .method = HTTP_POST,
        .handler = handlePostMode, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_mode);
    
    httpd_uri_t post_led = { .uri = "/api/led", .method = HTTP_POST,
        .handler = handlePostLed, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_led);
    
    httpd_uri_t post_led_next = { .uri = "/api/led/next", .method = HTTP_POST,
        .handler = handleLedNext, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_led_next);
    
    httpd_uri_t post_led_cycle_start = { .uri = "/api/led/cycle/start", .method = HTTP_POST,
        .handler = handleLedCycleStart, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_led_cycle_start);
    
    httpd_uri_t post_led_cycle_stop = { .uri = "/api/led/cycle/stop", .method = HTTP_POST,
        .handler = handleLedCycleStop, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_led_cycle_stop);
    
    httpd_uri_t post_logging_start = { .uri = "/api/logging/start", .method = HTTP_POST,
        .handler = handleLoggingStart, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_logging_start);
    
    httpd_uri_t post_logging_stop = { .uri = "/api/logging/stop", .method = HTTP_POST,
        .handler = handleLoggingStop, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_logging_stop);
    
    httpd_uri_t post_recover_session = { .uri = "/api/recovery/recover", .method = HTTP_POST,
        .handler = handlePostRecoverSession, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_recover_session);
    
    httpd_uri_t post_clear_recovery = { .uri = "/api/recovery/clear", .method = HTTP_POST,
        .handler = handlePostClearRecovery, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_clear_recovery);
    
    httpd_uri_t post_test_adc = { .uri = "/api/test/adc", .method = HTTP_POST,
        .handler = handleTestAdc, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_test_adc);
    
    httpd_uri_t post_test_led = { .uri = "/api/test/led", .method = HTTP_POST,
        .handler = handleTestLed, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_test_led);
    
    httpd_uri_t post_test_imu = { .uri = "/api/test/imu", .method = HTTP_POST,
        .handler = handleTestIMU, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_test_imu);
    
    httpd_uri_t post_test_rtc = { .uri = "/api/test/rtc", .method = HTTP_POST,
        .handler = handleTestRTC, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_test_rtc);
    
    httpd_uri_t post_test_sd = { .uri = "/api/test/sd", .method = HTTP_POST,
        .handler = handleTestSD, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_test_sd);
    
    httpd_uri_t post_test_neopixel = { .uri = "/api/test/neopixel", .method = HTTP_POST,
        .handler = handleTestLed, .user_ctx = nullptr };  // Alias for LED test
    httpd_register_uri_handler(server, &post_test_neopixel);
    
    httpd_uri_t post_test_battery = { .uri = "/api/test/battery", .method = HTTP_POST,
        .handler = handleTestBattery, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_test_battery);
    
    httpd_uri_t post_ota = { .uri = "/api/ota", .method = HTTP_POST,
        .handler = handleOtaUpload, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_ota);
    
    httpd_uri_t post_config = { .uri = "/api/config", .method = HTTP_POST,
        .handler = handlePostConfig, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &post_config);
    
    // ---- OPTIONS for CORS ----
    httpd_uri_t options_all = { .uri = "/api/*", .method = HTTP_OPTIONS,
        .handler = handleOptions, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &options_all);
    
    // ---- Root handler (explicit for /) ----
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET,
        .handler = handleStaticFile, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &root);
    
    // ---- Static file handler (wildcard, must be last) ----
    httpd_uri_t static_files = { .uri = "/*", .method = HTTP_GET,
        .handler = handleStaticFile, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &static_files);
    
    serverRunning = true;
    Serial.println("[WebUI] Server started on port 80");
    return true;
}

void stop() {
    if (server) {
        httpd_stop(server);
        server = nullptr;
    }
    serverRunning = false;
}

bool isRunning() {
    return serverRunning;
}

uint8_t getConnectionCount() {
    return 0;
}

} // namespace AdminWebUI
