/**
 * @file mock_arduino.h
 * @brief Mock Arduino types and functions for native unit testing
 * 
 * Provides minimal implementations of Arduino types needed to compile
 * source files for native (PC) unit testing without actual hardware.
 */

#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <atomic>

// ============================================================================
// Arduino Type Definitions
// ============================================================================

typedef uint8_t byte;
typedef bool boolean;

// ============================================================================
// Arduino Constants
// ============================================================================

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

#define PI 3.14159265358979323846
#define HALF_PI 1.5707963267948966
#define TWO_PI 6.283185307179586
#define DEG_TO_RAD 0.017453292519943295
#define RAD_TO_DEG 57.29577951308232

// ============================================================================
// ISR Attributes (no-op on native)
// ============================================================================

#define IRAM_ATTR
#define ICACHE_RAM_ATTR
#define PROGMEM

// ============================================================================
// Mock Time Functions
// ============================================================================

namespace MockArduino {
    extern uint32_t mockMillis;
    extern uint32_t mockMicros;
}

inline uint32_t millis() { return MockArduino::mockMillis; }
inline uint32_t micros() { return MockArduino::mockMicros; }
inline void delay(uint32_t ms) { MockArduino::mockMillis += ms; }
inline void delayMicroseconds(uint32_t us) { MockArduino::mockMicros += us; }

// ============================================================================
// Mock GPIO Functions (no-op)
// ============================================================================

inline void pinMode(uint8_t pin, uint8_t mode) {}
inline void digitalWrite(uint8_t pin, uint8_t val) {}
inline int digitalRead(uint8_t pin) { return 0; }
inline int analogRead(uint8_t pin) { return 0; }
inline void analogWrite(uint8_t pin, int val) {}

// ============================================================================
// Math Functions
// ============================================================================

inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

template<typename T>
inline T constrain(T x, T a, T b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

template<typename T>
inline T min(T a, T b) { return (a < b) ? a : b; }

template<typename T>
inline T max(T a, T b) { return (a > b) ? a : b; }

inline long abs(long x) { return (x < 0) ? -x : x; }

// ============================================================================
// String class stub
// ============================================================================

class String {
public:
    String() : buffer_("") {}
    String(const char* str) : buffer_(str ? str : "") {}
    String(int val) { 
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", val);
        buffer_ = buf;
    }
    String(unsigned int val) { 
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", val);
        buffer_ = buf;
    }
    String(long val) { 
        char buf[24];
        snprintf(buf, sizeof(buf), "%ld", val);
        buffer_ = buf;
    }
    String(float val, int decimalPlaces = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimalPlaces, val);
        buffer_ = buf;
    }
    
    const char* c_str() const { return buffer_.c_str(); }
    size_t length() const { return buffer_.length(); }
    bool isEmpty() const { return buffer_.empty(); }
    
    String& operator+=(const String& rhs) { buffer_ += rhs.buffer_; return *this; }
    String& operator+=(const char* rhs) { buffer_ += rhs; return *this; }
    String operator+(const String& rhs) const { return String((buffer_ + rhs.buffer_).c_str()); }
    
    bool operator==(const String& rhs) const { return buffer_ == rhs.buffer_; }
    bool operator==(const char* rhs) const { return buffer_ == rhs; }
    
    char charAt(unsigned int index) const { return buffer_[index]; }
    
private:
    std::string buffer_;
};

// ============================================================================
// Print class stub
// ============================================================================

class Print {
public:
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        while (size--) n += write(*buffer++);
        return n;
    }
    
    size_t print(const char* str) { return write((const uint8_t*)str, strlen(str)); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int n) { char buf[16]; snprintf(buf, 16, "%d", n); return print(buf); }
    size_t print(unsigned int n) { char buf[16]; snprintf(buf, 16, "%u", n); return print(buf); }
    size_t print(long n) { char buf[24]; snprintf(buf, 24, "%ld", n); return print(buf); }
    size_t print(float n, int decimals = 2) { char buf[32]; snprintf(buf, 32, "%.*f", decimals, n); return print(buf); }
    size_t print(const String& s) { return print(s.c_str()); }
    
    size_t println() { return print("\n"); }
    size_t println(const char* str) { return print(str) + println(); }
    size_t println(char c) { return print(c) + println(); }
    size_t println(int n) { return print(n) + println(); }
    size_t println(const String& s) { return print(s) + println(); }
    
    virtual int printf(const char* format, ...) { return 0; }
};

// ============================================================================
// Serial Mock
// ============================================================================

class MockSerial : public Print {
public:
    void begin(unsigned long baud) {}
    void end() {}
    
    virtual size_t write(uint8_t c) override { return 1; }
    
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    void flush() {}
    
    operator bool() { return true; }
};

extern MockSerial Serial;

// ============================================================================
// ESP32 specific stubs
// ============================================================================

class EspClass {
public:
    uint32_t getFreeHeap() { return 320000; }
    uint32_t getHeapSize() { return 320000; }
    uint32_t getChipId() { return 0x12345678; }
    const char* getSdkVersion() { return "mock"; }
    void restart() {}
};

extern EspClass ESP;

// ============================================================================
// Preferences Mock (NVS)
// ============================================================================

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false) { return true; }
    void end() {}
    
    bool putBool(const char* key, bool value) { return true; }
    bool putInt(const char* key, int32_t value) { return true; }
    bool putUInt(const char* key, uint32_t value) { return true; }
    bool putLong(const char* key, int32_t value) { return true; }
    bool putULong(const char* key, uint32_t value) { return true; }
    bool putLong64(const char* key, int64_t value) { return true; }
    bool putULong64(const char* key, uint64_t value) { return true; }
    bool putFloat(const char* key, float value) { return true; }
    bool putString(const char* key, const char* value) { return true; }
    bool putBytes(const char* key, const void* value, size_t len) { return true; }
    
    bool getBool(const char* key, bool defaultValue = false) { return defaultValue; }
    int32_t getInt(const char* key, int32_t defaultValue = 0) { return defaultValue; }
    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) { return defaultValue; }
    int32_t getLong(const char* key, int32_t defaultValue = 0) { return defaultValue; }
    uint32_t getULong(const char* key, uint32_t defaultValue = 0) { return defaultValue; }
    int64_t getLong64(const char* key, int64_t defaultValue = 0) { return defaultValue; }
    uint64_t getULong64(const char* key, uint64_t defaultValue = 0) { return defaultValue; }
    float getFloat(const char* key, float defaultValue = 0) { return defaultValue; }
    String getString(const char* key, const char* defaultValue = "") { return String(defaultValue); }
    size_t getBytes(const char* key, void* buf, size_t maxLen) { return 0; }
    
    bool remove(const char* key) { return true; }
    bool clear() { return true; }
};

#endif // MOCK_ARDUINO_H




