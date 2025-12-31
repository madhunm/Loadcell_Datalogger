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
 * @brief Initialize the web server routes
 * 
 * Sets up SPIFFS and configures HTTP routes.
 * Call BEFORE WiFi AP is started.
 * Does NOT start the server - call beginServer() after WiFi is ready.
 * 
 * @return true if routes configured successfully
 */
bool init();

/**
 * @brief Start the HTTP server
 * 
 * Actually starts listening for connections.
 * Call from WiFi event handler when AP is fully ready.
 * 
 * @return true if server started successfully
 */
bool beginServer();

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

