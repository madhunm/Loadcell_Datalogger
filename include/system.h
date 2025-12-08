#pragma once

#include <Arduino.h>

// System operational states
enum SystemState
{
    STATE_INIT = 0,        ///< Initialization state - bringing up peripherals
    STATE_READY,           ///< Ready state - waiting for user to start logging
    STATE_LOGGING,         ///< Logging state - actively recording data
    STATE_CONVERTING       ///< Converting state - converting binary logs to CSV
};

// Get current system state (thread-safe)
SystemState systemGetState();

// Remote logging control (thread-safe flags)
// These are defined in main.cpp
extern volatile bool g_remoteLoggingRequest;
extern volatile bool g_remoteLoggingAction; // false = stop, true = start

