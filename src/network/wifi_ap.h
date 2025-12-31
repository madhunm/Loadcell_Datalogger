/**
 * @file wifi_ap.h
 * @brief WiFi Access Point management for admin WebUI
 */

#ifndef WIFI_AP_H
#define WIFI_AP_H

#include <Arduino.h>
#include <WiFi.h>

/**
 * @brief Manages WiFi Access Point for admin WebUI access
 */
class WiFiAP {
public:
    /**
     * @brief Start WiFi Access Point
     * @return true if successful, false otherwise
     */
    bool start();
    
    /**
     * @brief Stop WiFi Access Point
     * @return true if successful
     */
    bool stop();
    
    /**
     * @brief Check if AP is active
     * @return true if running, false otherwise
     */
    bool isActive();
    
    /**
     * @brief Get AP SSID
     * @return SSID string
     */
    String getSSID() const { return ap_ssid; }
    
    /**
     * @brief Get AP IP address
     * @return IP address string
     */
    String getIPAddress() const { return ap_ip; }
    
    /**
     * @brief Get number of connected clients
     * @return Client count
     */
    uint8_t getClientCount();
    
private:
    String ap_ssid;
    String ap_ip;
    bool active = false;
    
    /**
     * @brief Generate SSID from MAC address
     */
    String generateSSID();
};

#endif // WIFI_AP_H
