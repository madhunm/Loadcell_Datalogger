/**
 * @file wifi_ap.h
 * @brief WiFi Access Point Manager for Loadcell Data Logger
 * 
 * Manages WiFi in AP (Access Point) mode for the admin WebUI.
 * SSID format: LoadcellLogger-XXXX (last 4 hex digits of MAC)
 * Default IP: 192.168.4.1
 */

#ifndef WIFI_AP_H
#define WIFI_AP_H

#include <Arduino.h>

namespace WiFiAP {

/**
 * @brief WiFi AP configuration
 */
struct Config {
    const char* ssid_prefix = "LoadcellLogger";
    const char* password = nullptr;  // Open network (null = no password)
    uint8_t channel = 1;
    bool hidden = false;
    uint8_t max_connections = 4;
};

/**
 * @brief Initialize and start the WiFi AP
 * 
 * @param config Optional configuration (uses defaults if nullptr)
 * @return true if AP started successfully
 */
bool start(const Config* config = nullptr);

/**
 * @brief Stop the WiFi AP
 */
void stop();

/**
 * @brief Check if WiFi AP is active
 * 
 * @return true if AP is running
 */
bool isActive();

/**
 * @brief Check if WiFi AP is ready for server start
 * 
 * Returns true once the ARDUINO_EVENT_WIFI_AP_START event has fired,
 * indicating the TCP/IP stack is ready for connections.
 * 
 * @return true if AP is fully ready
 */
bool isReady();

/**
 * @brief Get the AP SSID
 * 
 * @return SSID string (includes MAC suffix)
 */
const char* getSSID();

/**
 * @brief Get the AP IP address
 * 
 * @return IP address string
 */
String getIP();

/**
 * @brief Get number of connected clients
 * 
 * @return Number of connected stations
 */
uint8_t getClientCount();

} // namespace WiFiAP

#endif // WIFI_AP_H

