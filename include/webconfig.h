#pragma once

#include <Arduino.h>
#include "logger.h"

// Initialize WiFi access point and web server
// SSID will be "Loadcell_Datalogger_<random_number>"
// Returns true on success
bool webConfigInit();

// Handle web server requests (call from loop())
void webConfigHandleClient();

// Get current logger configuration
LoggerConfig webConfigGetLoggerConfig();

// Set logger configuration (from web form)
void webConfigSetLoggerConfig(const LoggerConfig &config);

// Check if web config is active
bool webConfigIsActive();


