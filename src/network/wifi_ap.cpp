/**
 * @file wifi_ap.cpp
 * @brief Implementation of WiFi Access Point management
 */

#include "wifi_ap.h"

String WiFiAP::generateSSID() {
    // Get MAC address and use last 4 hex digits
    uint8_t mac[6];
    WiFi.macAddress(mac);
    
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "LoadcellLogger-%02X%02X", mac[4], mac[5]);
    
    return String(ssid);
}

bool WiFiAP::start() {
    if (active) {
        return true;  // Already started
    }
    
    // Generate SSID
    ap_ssid = generateSSID();
    
    // Configure and start AP (no password - physical access = admin)
    WiFi.mode(WIFI_AP);
    
    if (!WiFi.softAP(ap_ssid.c_str())) {
        Serial.println("WiFi: Failed to start AP");
        return false;
    }
    
    // Get IP address
    IPAddress ip = WiFi.softAPIP();
    ap_ip = ip.toString();
    
    active = true;
    
    Serial.println("WiFi: Access Point started");
    Serial.printf("WiFi: SSID: %s\n", ap_ssid.c_str());
    Serial.printf("WiFi: IP: %s\n", ap_ip.c_str());
    Serial.println("WiFi: No password required");
    
    return true;
}

bool WiFiAP::stop() {
    if (!active) {
        return true;  // Already stopped
    }
    
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    
    active = false;
    
    Serial.println("WiFi: Access Point stopped");
    
    return true;
}

bool WiFiAP::isActive() {
    return active && (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
}

uint8_t WiFiAP::getClientCount() {
    return WiFi.softAPgetStationNum();
}
