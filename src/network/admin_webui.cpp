/**
 * @file admin_webui.cpp
 * @brief Admin WebUI HTTP Server Implementation
 */

#include "admin_webui.h"
#include "../app/app_mode.h"
#include "../drivers/status_led.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

namespace AdminWebUI {

namespace {
    AsyncWebServer* server = nullptr;
    bool serverRunning = false;
    
    // Helper to send JSON response
    void sendJson(AsyncWebServerRequest* request, JsonDocument& doc, int code = 200) {
        String response;
        serializeJson(doc, response);
        request->send(code, "application/json", response);
    }
    
    // Helper to send error response
    void sendError(AsyncWebServerRequest* request, const char* message, int code = 400) {
        JsonDocument doc;
        doc["success"] = false;
        doc["error"] = message;
        sendJson(request, doc, code);
    }
    
    // Helper to send success response
    void sendSuccess(AsyncWebServerRequest* request, const char* message = nullptr) {
        JsonDocument doc;
        doc["success"] = true;
        if (message) {
            doc["message"] = message;
        }
        sendJson(request, doc);
    }
    
    // ========================================================================
    // API Handlers
    // ========================================================================
    
    void handleGetMode(AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["mode"] = AppMode::getModeString();
        sendJson(request, doc);
    }
    
    void handleSetMode(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (error) {
            sendError(request, "Invalid JSON");
            return;
        }
        
        const char* modeStr = doc["mode"];
        const char* password = doc["password"];
        
        if (!modeStr) {
            sendError(request, "Missing mode parameter");
            return;
        }
        
        AppMode::Mode newMode;
        if (strcmp(modeStr, "user") == 0) {
            newMode = AppMode::Mode::User;
        } else if (strcmp(modeStr, "admin") == 0) {
            newMode = AppMode::Mode::FieldAdmin;
        } else if (strcmp(modeStr, "factory") == 0) {
            newMode = AppMode::Mode::Factory;
        } else {
            sendError(request, "Invalid mode");
            return;
        }
        
        bool success = AppMode::setMode(newMode, password);
        
        if (success) {
            // Update LED state based on new mode
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
            
            JsonDocument response;
            response["success"] = true;
            response["mode"] = AppMode::getModeString();
            sendJson(request, response);
        } else {
            sendError(request, "Invalid password", 401);
        }
    }
    
    void handleGetStatus(AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        doc["mode"] = AppMode::getModeString();
        doc["wifi"] = true;
        doc["sd_present"] = true;  // TODO: Implement actual SD check
        doc["logging"] = false;    // TODO: Implement logging state
        doc["uptime_ms"] = millis();
        doc["free_heap"] = ESP.getFreeHeap();
        
        sendJson(request, doc);
    }
    
    void handleGetConfig(AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        // TODO: Load from NVS
        doc["loadcell_id"] = "TC023L0-000025";
        doc["loadcell_model"] = "TC023L0";
        doc["loadcell_serial"] = "000025";
        doc["capacity_kg"] = 2000.0;
        doc["excitation_V"] = 10.0;
        doc["adc_pga_gain"] = 128;
        doc["imu_g_range"] = 16;
        doc["imu_gyro_dps"] = 2000;
        
        JsonArray points = doc["calibration_points"].to<JsonArray>();
        JsonObject p1 = points.add<JsonObject>();
        p1["load_kg"] = 0;
        p1["output_uV"] = 0;
        JsonObject p2 = points.add<JsonObject>();
        p2["load_kg"] = 1000;
        p2["output_uV"] = 5000;
        JsonObject p3 = points.add<JsonObject>();
        p3["load_kg"] = 2000;
        p3["output_uV"] = 10000;
        
        sendJson(request, doc);
    }
    
    void handleSetConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (error) {
            sendError(request, "Invalid JSON");
            return;
        }
        
        // Check if mode allows configuration
        if (!AppMode::canConfigure()) {
            sendError(request, "Configuration not allowed in current mode", 403);
            return;
        }
        
        // TODO: Save to NVS
        Serial.println("[WebUI] Config received (not yet saved to NVS)");
        
        sendSuccess(request, "Configuration saved");
    }
    
    void handleGetSDCard(AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        // TODO: Implement actual SD card stats
        doc["present"] = true;
        doc["total_mb"] = 32768;
        doc["used_mb"] = 1234;
        doc["free_mb"] = 31534;
        
        JsonArray files = doc["files"].to<JsonArray>();
        // TODO: List actual files
        
        sendJson(request, doc);
    }
    
    void handleGetBattery(AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        // TODO: Implement actual battery reading
        doc["voltage_mV"] = 3850;
        doc["percent"] = 75;
        doc["charging"] = false;
        
        sendJson(request, doc);
    }
    
    void handleGetLive(AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        // TODO: Implement actual sensor readings
        doc["timestamp_ms"] = millis();
        doc["load_kg"] = 500.0 + (random(-100, 100) / 10.0);
        doc["raw_adc"] = 5000000 + random(-10000, 10000);
        doc["accel_x"] = random(-100, 100) / 1000.0;
        doc["accel_y"] = random(-100, 100) / 1000.0;
        doc["accel_z"] = 1.0 + random(-50, 50) / 1000.0;
        doc["gyro_x"] = random(-100, 100) / 10.0;
        doc["gyro_y"] = random(-100, 100) / 10.0;
        doc["gyro_z"] = random(-100, 100) / 10.0;
        
        sendJson(request, doc);
    }
    
    void handleSensorTest(AsyncWebServerRequest* request, const String& sensor) {
        // Check if in factory mode
        if (!AppMode::canFactoryTest()) {
            sendError(request, "Factory tests only available in Factory mode", 403);
            return;
        }
        
        JsonDocument doc;
        doc["sensor"] = sensor;
        
        // Set LED to testing state
        StatusLED::setState(StatusLED::State::FactoryTesting);
        
        // Simulate test (TODO: implement actual tests)
        bool passed = random(0, 10) > 1;  // 90% pass rate
        
        doc["passed"] = passed;
        doc["message"] = passed ? (sensor + " test passed") : (sensor + " test failed");
        
        JsonObject details = doc["details"].to<JsonObject>();
        
        if (sensor == "adc") {
            details["raw_value"] = 8388608;
            details["noise_uV"] = 0.5;
        } else if (sensor == "imu") {
            details["accel_z"] = 1.0;
        } else if (sensor == "rtc") {
            details["time"] = "2024-12-31T12:00:00";
            details["valid"] = true;
        } else if (sensor == "sd") {
            details["type"] = "SDHC";
            details["size_gb"] = 32;
        } else if (sensor == "neopixel") {
            details["colors_tested"] = 6;
        }
        
        // Return to idle state
        StatusLED::setState(StatusLED::State::IdleFactory);
        
        sendJson(request, doc);
    }
    
    void handleLoggingStart(AsyncWebServerRequest* request) {
        if (!AppMode::canLog()) {
            sendError(request, "Logging not allowed in current mode", 403);
            return;
        }
        
        // TODO: Start actual logging
        StatusLED::setState(StatusLED::State::Logging);
        sendSuccess(request, "Logging started");
    }
    
    void handleLoggingStop(AsyncWebServerRequest* request) {
        // TODO: Stop actual logging
        
        // Return to appropriate idle state
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
        
        sendSuccess(request, "Logging stopped");
    }
    
    // ========================================================================
    // LED Test API Handlers
    // ========================================================================
    
    void handleGetLed(AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        doc["state_index"] = StatusLED::getTestStateIndex();
        doc["state_count"] = StatusLED::getTestStateCount();
        doc["state_name"] = StatusLED::getTestStateName();
        doc["cycling"] = StatusLED::isTestCycling();
        
        sendJson(request, doc);
    }
    
    void handleSetLed(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
        // Only allow LED control in Factory mode
        if (!AppMode::canFactoryTest()) {
            sendError(request, "LED test only available in Factory mode", 403);
            return;
        }
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (error) {
            sendError(request, "Invalid JSON");
            return;
        }
        
        // Get color (required)
        const char* colorStr = doc["color"];
        if (!colorStr) {
            sendError(request, "Missing color parameter");
            return;
        }
        
        // Parse color
        StatusLED::Color color = StatusLED::Colors::Off;
        if (strcmp(colorStr, "off") == 0) {
            color = StatusLED::Colors::Off;
        } else if (strcmp(colorStr, "red") == 0) {
            color = StatusLED::Colors::Red;
        } else if (strcmp(colorStr, "green") == 0) {
            color = StatusLED::Colors::Green;
        } else if (strcmp(colorStr, "blue") == 0) {
            color = StatusLED::Colors::Blue;
        } else if (strcmp(colorStr, "cyan") == 0) {
            color = StatusLED::Colors::Cyan;
        } else if (strcmp(colorStr, "orange") == 0) {
            color = StatusLED::Colors::Orange;
        } else if (strcmp(colorStr, "magenta") == 0) {
            color = StatusLED::Colors::Magenta;
        } else {
            sendError(request, "Invalid color. Use: off, red, green, blue, cyan, orange, magenta");
            return;
        }
        
        // Get pattern (optional, default: steady)
        StatusLED::Pattern pattern = StatusLED::Pattern::Steady;
        const char* patternStr = doc["pattern"];
        if (patternStr) {
            if (strcmp(patternStr, "off") == 0) {
                pattern = StatusLED::Pattern::Off;
            } else if (strcmp(patternStr, "steady") == 0) {
                pattern = StatusLED::Pattern::Steady;
            } else if (strcmp(patternStr, "pulse") == 0) {
                pattern = StatusLED::Pattern::Pulse;
            } else if (strcmp(patternStr, "fast_blink") == 0) {
                pattern = StatusLED::Pattern::FastBlink;
            } else if (strcmp(patternStr, "very_fast_blink") == 0) {
                pattern = StatusLED::Pattern::VeryFastBlink;
            } else if (strcmp(patternStr, "blink_code") == 0) {
                pattern = StatusLED::Pattern::BlinkCode;
            } else {
                sendError(request, "Invalid pattern. Use: off, steady, pulse, fast_blink, very_fast_blink, blink_code");
                return;
            }
        }
        
        // Get blink count (optional, for blink_code pattern)
        uint8_t blinkCount = doc["blink_count"] | 1;
        if (blinkCount < 1) blinkCount = 1;
        if (blinkCount > 6) blinkCount = 6;
        
        // Apply the settings
        StatusLED::setTestMode(color, pattern, blinkCount);
        
        JsonDocument response;
        response["success"] = true;
        response["color"] = colorStr;
        response["pattern"] = patternStr ? patternStr : "steady";
        response["blink_count"] = blinkCount;
        sendJson(request, response);
    }
    
    void handleLedNext(AsyncWebServerRequest* request) {
        // Only allow LED control in Factory mode
        if (!AppMode::canFactoryTest()) {
            sendError(request, "LED test only available in Factory mode", 403);
            return;
        }
        
        StatusLED::nextTestState();
        
        JsonDocument doc;
        doc["success"] = true;
        doc["state_index"] = StatusLED::getTestStateIndex();
        doc["state_count"] = StatusLED::getTestStateCount();
        doc["state_name"] = StatusLED::getTestStateName();
        sendJson(request, doc);
    }
    
    void handleLedCycleStart(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
        // Only allow LED control in Factory mode
        if (!AppMode::canFactoryTest()) {
            sendError(request, "LED test only available in Factory mode", 403);
            return;
        }
        
        uint16_t interval_ms = 1500; // Default 1.5 seconds
        
        if (len > 0) {
            JsonDocument doc;
            if (!deserializeJson(doc, data, len)) {
                interval_ms = doc["interval_ms"] | 1500;
            }
        }
        
        // Clamp interval to reasonable range
        if (interval_ms < 500) interval_ms = 500;
        if (interval_ms > 5000) interval_ms = 5000;
        
        StatusLED::startTestCycle(interval_ms);
        
        JsonDocument response;
        response["success"] = true;
        response["cycling"] = true;
        response["interval_ms"] = interval_ms;
        sendJson(request, response);
    }
    
    void handleLedCycleStop(AsyncWebServerRequest* request) {
        StatusLED::stopTestCycle();
        
        JsonDocument doc;
        doc["success"] = true;
        doc["cycling"] = false;
        sendJson(request, doc);
    }
    
    void handleNotFound(AsyncWebServerRequest* request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
            return;
        }
        request->send(404, "text/plain", "Not Found");
    }
    
    // Setup CORS headers
    void addCorsHeaders(AsyncWebServerResponse* response) {
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    }

} // anonymous namespace

bool init() {
    // Initialize SPIFFS for static files
    if (!SPIFFS.begin(true)) {
        Serial.println("[WebUI] SPIFFS mount failed");
        return false;
    }
    
    server = new AsyncWebServer(80);
    
    // Add default CORS headers to all responses
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
    
    // ========================================================================
    // API Routes
    // ========================================================================
    
    // Mode
    server->on("/api/mode", HTTP_GET, handleGetMode);
    server->on("/api/mode", HTTP_POST, [](AsyncWebServerRequest* request){},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handleSetMode(request, data, len);
        }
    );
    
    // Status
    server->on("/api/status", HTTP_GET, handleGetStatus);
    
    // Config
    server->on("/api/config", HTTP_GET, handleGetConfig);
    server->on("/api/config", HTTP_POST, [](AsyncWebServerRequest* request){},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handleSetConfig(request, data, len);
        }
    );
    
    // SD Card
    server->on("/api/sdcard", HTTP_GET, handleGetSDCard);
    
    // Battery
    server->on("/api/battery", HTTP_GET, handleGetBattery);
    
    // Live data
    server->on("/api/live", HTTP_GET, handleGetLive);
    
    // Sensor tests
    server->on("/api/test/adc", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleSensorTest(request, "adc");
    });
    server->on("/api/test/imu", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleSensorTest(request, "imu");
    });
    server->on("/api/test/rtc", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleSensorTest(request, "rtc");
    });
    server->on("/api/test/sd", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleSensorTest(request, "sd");
    });
    server->on("/api/test/neopixel", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleSensorTest(request, "neopixel");
    });
    
    // Logging control
    server->on("/api/logging/start", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleLoggingStart(request);
    });
    server->on("/api/logging/stop", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleLoggingStop(request);
    });
    
    // LED test control (Factory mode)
    server->on("/api/led", HTTP_GET, handleGetLed);
    server->on("/api/led", HTTP_POST, [](AsyncWebServerRequest* request){},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handleSetLed(request, data, len);
        }
    );
    server->on("/api/led/next", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleLedNext(request);
    });
    server->on("/api/led/cycle/start", HTTP_POST, [](AsyncWebServerRequest* request){},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handleLedCycleStart(request, data, len);
        }
    );
    server->on("/api/led/cycle/stop", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleLedCycleStop(request);
    });
    
    // ========================================================================
    // Static Files
    // ========================================================================
    
    server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    // 404 handler
    server->onNotFound(handleNotFound);
    
    Serial.println("[WebUI] Routes configured (server not started yet)");
    return true;
}

bool beginServer() {
    if (!server) {
        Serial.println("[WebUI] Error: init() must be called before beginServer()");
        return false;
    }
    
    if (serverRunning) {
        Serial.println("[WebUI] Server already running");
        return true;
    }
    
    // AsyncTCP 3.x handles TCPIP locking internally - no wrapper needed
    server->begin();
    serverRunning = true;
    
    Serial.println("[WebUI] Server started on port 80");
    return true;
}

void stop() {
    if (server) {
        server->end();
        delete server;
        server = nullptr;
    }
    serverRunning = false;
    Serial.println("[WebUI] Server stopped");
}

bool isRunning() {
    return serverRunning;
}

uint8_t getConnectionCount() {
    // ESPAsyncWebServer doesn't expose connection count easily
    return 0;
}

} // namespace AdminWebUI

