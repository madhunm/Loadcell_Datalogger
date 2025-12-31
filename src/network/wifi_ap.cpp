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

