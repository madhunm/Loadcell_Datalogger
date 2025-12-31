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
#include <esp_http_server.h>
#include <SPIFFS.h>

namespace AdminWebUI {

namespace {
    httpd_handle_t server = nullptr;
    bool serverRunning = false;
    
    // Reusable response buffer
    char jsonBuf[512];
    
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
        
        if (rawAdc != INT32_MIN) {
            float uV = MAX11270::rawToMicrovolts(rawAdc);
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\"timestamp_ms\":%lu,\"raw_adc\":%ld,\"voltage_uV\":%.2f,\"load_kg\":%.4f,"
                "\"adc_samples\":%lu,\"adc_dropped\":%lu}",
                millis(), (long)rawAdc, uV, uV / 10.0,
                (unsigned long)stats.samplesAcquired, (unsigned long)stats.samplesDropped
            );
        } else {
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\"timestamp_ms\":%lu,\"raw_adc\":0,\"adc_error\":true,"
                "\"adc_samples\":%lu,\"adc_dropped\":%lu}",
                millis(),
                (unsigned long)stats.samplesAcquired, (unsigned long)stats.samplesDropped
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
    config.max_uri_handlers = 20;
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
    
    httpd_uri_t get_led = { .uri = "/api/led", .method = HTTP_GET,
        .handler = handleGetLed, .user_ctx = nullptr };
    httpd_register_uri_handler(server, &get_led);
    
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
