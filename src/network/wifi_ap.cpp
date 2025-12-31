/**
 * @file wifi_ap.cpp
 * @brief WiFi Access Point Manager Implementation
 */

#include "wifi_ap.h"
#include <WiFi.h>

namespace WiFiAP {

namespace {
    char ssidBuffer[32] = {0};
    bool apActive = false;
    volatile bool apReady = false;  // Set by event handler when TCP/IP stack is ready
    
    /**
     * @brief WiFi event handler
     * 
     * Called by Arduino WiFi subsystem when events occur.
     * Sets a flag when AP is ready - actual server start is deferred to main loop
     * to avoid TCP/IP core lock assertion failures.
     */
    void onWiFiEvent(WiFiEvent_t event) {
        switch (event) {
            case ARDUINO_EVENT_WIFI_AP_START:
                Serial.println("[WiFiAP] Event: AP Started - ready for server");
                apReady = true;  // Signal main loop to start server
                break;
            case ARDUINO_EVENT_WIFI_AP_STOP:
                Serial.println("[WiFiAP] Event: AP Stopped");
                apReady = false;
                break;
            case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
                Serial.println("[WiFiAP] Event: Client connected");
                break;
            case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
                Serial.println("[WiFiAP] Event: Client disconnected");
                break;
            default:
                break;
        }
    }
}

bool start(const Config* config) {
    Config cfg;
    if (config) {
        cfg = *config;
    }
    
    // Generate SSID with MAC suffix
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(ssidBuffer, sizeof(ssidBuffer), "%s-%02X%02X", 
             cfg.ssid_prefix, mac[4], mac[5]);
    
    // Register event handler BEFORE starting AP
    // This ensures we catch the ARDUINO_EVENT_WIFI_AP_START event
    WiFi.onEvent(onWiFiEvent);
    
    // Configure AP
    WiFi.mode(WIFI_AP);
    
    bool success;
    if (cfg.password && strlen(cfg.password) >= 8) {
        success = WiFi.softAP(ssidBuffer, cfg.password, cfg.channel, 
                              cfg.hidden, cfg.max_connections);
    } else {
        // Open network
        success = WiFi.softAP(ssidBuffer, nullptr, cfg.channel, 
                              cfg.hidden, cfg.max_connections);
    }
    
    if (success) {
        apActive = true;
        Serial.println("[WiFiAP] Started");
        Serial.println("[WiFiAP] SSID: " + String(ssidBuffer));
        Serial.println("[WiFiAP] IP: " + WiFi.softAPIP().toString());
    } else {
        Serial.println("[WiFiAP] Failed to start");
    }
    
    return success;
}

void stop() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    apActive = false;
    Serial.println("[WiFiAP] Stopped");
}

bool isActive() {
    return apActive;
}

bool isReady() {
    return apReady;
}

const char* getSSID() {
    return ssidBuffer;
}

String getIP() {
    return WiFi.softAPIP().toString();
}

uint8_t getClientCount() {
    return WiFi.softAPgetStationNum();
}

} // namespace WiFiAP

