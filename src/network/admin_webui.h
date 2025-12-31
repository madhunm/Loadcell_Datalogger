/**
 * @file admin_webui.h
 * @brief Admin WebUI HTTP Server for Loadcell Data Logger
 * 
 * Provides REST API endpoints and serves static files from SPIFFS.
 * 
 * API Endpoints:
 *   GET  /api/mode        - Get current mode
 *   POST /api/mode        - Switch mode (requires password)
 *   GET  /api/status      - System status
 *   GET  /api/config      - Get configuration
 *   POST /api/config      - Save configuration
 *   GET  /api/sdcard      - SD card statistics
 *   GET  /api/battery     - Battery level
 *   GET  /api/live        - Live sensor data
 *   POST /api/test/{name} - Run sensor test (Factory mode)
 *   POST /api/logging/start - Start logging
 *   POST /api/logging/stop  - Stop logging
 */

#ifndef ADMIN_WEBUI_H
#define ADMIN_WEBUI_H

#include <Arduino.h>

namespace AdminWebUI {

/**
 * @brief Initialize the web server
 * 
 * Sets up routes and starts the async HTTP server.
 * Call after WiFi AP is started.
 * 
 * @return true if server started successfully
 */
bool init();

/**
 * @brief Stop the web server
 */
void stop();

/**
 * @brief Check if web server is running
 * 
 * @return true if server is active
 */
bool isRunning();

/**
 * @brief Get the number of active connections
 * 
 * @return Number of HTTP connections
 */
uint8_t getConnectionCount();

} // namespace AdminWebUI

#endif // ADMIN_WEBUI_H

