#pragma once

#include <Arduino.h>
#include "FS.h"

// Forward declaration of FS type from the fs namespace
namespace fs
{
    class FS;
}

// SD card initialisation and access helpers
bool sdCardInit();
bool sdCardIsMounted();
fs::FS &sdCardGetFs();

// Check if SD card is physically present using card detect pin
// Returns true if card is present, false if removed
// This should be called periodically during logging to detect removal
bool sdCardCheckPresent();

// Get free space on SD card in bytes (returns 0 if not mounted)
uint64_t sdCardGetFreeSpace();

// Get total space on SD card in bytes (returns 0 if not mounted)
uint64_t sdCardGetTotalSpace();