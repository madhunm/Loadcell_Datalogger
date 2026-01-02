/**
 * @file mock_arduino.cpp
 * @brief Mock Arduino implementation for native tests
 */

#include "mock_arduino.h"

// Mock time state
namespace MockArduino {
    uint32_t mockMillis = 0;
    uint32_t mockMicros = 0;
}

// Global instances
MockSerial Serial;
EspClass ESP;




