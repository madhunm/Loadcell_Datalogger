/**
 * @file admin_webui.cpp
 * @brief Admin WebUI HTTP Server Implementation (ESP-IDF Native)
 * 
 * Flash-optimized implementation using:
 * - ESP-IDF native esp_http_server (no external libraries)
 * - Manual JSON string building (no ArduinoJson)
 * - Minimal memory footprint
 */

#include "admin_webui.h"
#include "../app/app_mode.h"
#include "../drivers/status_led.h"
#include "../drivers/max11270.h"
#include "../drivers/max17048.h"
#include "../drivers/sd_manager.h"
#include "../drivers/lsm6dsv.h"
#include "../drivers/rx8900ce.h"
#include <esp_http_server.h>
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
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"mode\":\"%s\",\"wifi\":true,\"uptime_ms\":%lu,\"free_heap\":%u,\"adc_present\":%s}",
            AppMode::getModeString(),
            millis(),
            ESP.getFreeHeap(),
            MAX11270::isPresent() ? "true" : "false"
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
        if (!AppMode::canLog()) {
            sendError(req, "Logging not allowed", 403);
            return ESP_OK;
        }
        StatusLED::setState(StatusLED::State::Logging);
        sendSuccess(req, "Logging started");
        return ESP_OK;
    }
    
    esp_err_t handleLoggingStop(httpd_req_t* req) {
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
    
    // Server-Sent Events (SSE) stream for real-time sensor data
    esp_err_t handleStream(httpd_req_t* req) {
        // Set SSE headers
        httpd_resp_set_type(req, "text/event-stream");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        
        char sseBuf[256];
        
        Serial.println("[SSE] Client connected to stream");
        
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
            
            // Format SSE message
            int len = snprintf(sseBuf, sizeof(sseBuf),
                "data: {\"adc\":%ld,\"uV\":%.1f,\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
                "\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,\"t\":%lu}\n\n",
                (long)rawAdc, uV, ax, ay, az, gx, gy, gz, millis());
            
            // Send chunk - if this fails, client disconnected
            if (httpd_resp_send_chunk(req, sseBuf, len) != ESP_OK) {
                Serial.println("[SSE] Client disconnected");
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
    
    Serial.println("[WebUI] SPIFFS mounted, routes ready");
    return true;
}

bool beginServer() {
    if (serverRunning) return true;
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 25;
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
    
    // ---- OPTIONS for CORS ----
    httpd_uri_t options_all = { .uri = "/api/*", .method = HTTP_OPTIONS,
        .handler = handleOptions, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &options_all);
    
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
