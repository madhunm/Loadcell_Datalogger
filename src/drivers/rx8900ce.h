/**
 * @file rx8900ce.h
 * @brief RX8900CE Real-Time Clock Driver
 * 
 * Features:
 * - I2C communication at 400kHz (address 0x32)
 * - BCD time/date read and write
 * - 1Hz FOUT output for timestamp discipline
 * - Temperature-compensated crystal oscillator (TCXO)
 * - Battery backup support
 */

#ifndef RX8900CE_H
#define RX8900CE_H

#include <Arduino.h>
#include <time.h>

namespace RX8900CE {

// ============================================================================
// Register Definitions
// ============================================================================

namespace Reg {
    // Time registers (BCD format)
    constexpr uint8_t SEC       = 0x00;
    constexpr uint8_t MIN       = 0x01;
    constexpr uint8_t HOUR      = 0x02;
    constexpr uint8_t WEEK      = 0x03;  // Day of week (0=Sunday)
    constexpr uint8_t DAY       = 0x04;
    constexpr uint8_t MONTH     = 0x05;
    constexpr uint8_t YEAR      = 0x06;
    
    // Alarm registers
    constexpr uint8_t MIN_ALARM   = 0x08;
    constexpr uint8_t HOUR_ALARM  = 0x09;
    constexpr uint8_t WEEK_ALARM  = 0x0A;
    constexpr uint8_t DAY_ALARM   = 0x0A;
    
    // Timer registers
    constexpr uint8_t TIMER_CNT0  = 0x0B;
    constexpr uint8_t TIMER_CNT1  = 0x0C;
    
    // Control registers
    constexpr uint8_t EXTENSION   = 0x0D;
    constexpr uint8_t FLAG        = 0x0E;
    constexpr uint8_t CONTROL     = 0x0F;
    
    // Additional registers
    constexpr uint8_t TEMP        = 0x17;  // Temperature (2's complement, 0.25°C/LSB, offset -60°C at 0x00)
    constexpr uint8_t BACKUP      = 0x18;
}

// ============================================================================
// Bit Definitions
// ============================================================================

namespace Bits {
    // EXTENSION register (0x0D)
    constexpr uint8_t TSEL0     = 0x01;  // Timer clock select bit 0
    constexpr uint8_t TSEL1     = 0x02;  // Timer clock select bit 1
    constexpr uint8_t FSEL0     = 0x04;  // FOUT frequency select bit 0
    constexpr uint8_t FSEL1     = 0x08;  // FOUT frequency select bit 1
    constexpr uint8_t TE        = 0x10;  // Timer enable
    constexpr uint8_t USEL      = 0x20;  // Update interrupt select
    constexpr uint8_t WADA      = 0x40;  // Week/Day alarm select
    constexpr uint8_t TEST      = 0x80;  // Test mode (always write 0)
    
    // FLAG register (0x0E)
    constexpr uint8_t VDET      = 0x01;  // Voltage detect flag
    constexpr uint8_t VLF       = 0x02;  // Voltage low flag (data may be invalid)
    constexpr uint8_t AF        = 0x08;  // Alarm flag
    constexpr uint8_t TF        = 0x10;  // Timer flag
    constexpr uint8_t UF        = 0x20;  // Update flag
    
    // CONTROL register (0x0F)
    constexpr uint8_t RESET     = 0x01;  // Reset (write 1, auto-clears)
    constexpr uint8_t AIE       = 0x08;  // Alarm interrupt enable
    constexpr uint8_t TIE       = 0x10;  // Timer interrupt enable
    constexpr uint8_t UIE       = 0x20;  // Update interrupt enable
    constexpr uint8_t CSEL0     = 0x40;  // Temperature compensation interval bit 0
    constexpr uint8_t CSEL1     = 0x80;  // Temperature compensation interval bit 1
}

// ============================================================================
// FOUT Frequency Options
// ============================================================================

enum class FOUTFreq : uint8_t {
    Hz32768 = 0x00,  // 32.768 kHz
    Hz1024  = 0x04,  // 1024 Hz
    Hz1     = 0x08,  // 1 Hz (for timestamp discipline)
    Off     = 0x0C   // FOUT disabled (high impedance)
};

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize the RTC driver
 * 
 * Sets up I2C communication and verifies the chip is present.
 * Does NOT initialize the I2C bus - caller must do that first.
 * 
 * @return true if initialization successful
 */
bool init();

/**
 * @brief Check if RTC is present and communicating
 * 
 * @return true if RTC responds to I2C
 */
bool isPresent();

/**
 * @brief Check if RTC data is valid
 * 
 * Returns false if VLF flag is set, indicating battery was
 * too low and time data may be invalid.
 * 
 * @return true if time data is valid
 */
bool isTimeValid();

/**
 * @brief Get current time from RTC
 * 
 * @param t Pointer to struct tm to fill with current time
 * @return true if read successful
 */
bool getTime(struct tm* t);

/**
 * @brief Set RTC time
 * 
 * @param t Pointer to struct tm with time to set
 * @return true if write successful
 */
bool setTime(const struct tm* t);

/**
 * @brief Get Unix epoch time
 * 
 * @return Unix timestamp (seconds since 1970-01-01 00:00:00 UTC)
 */
time_t getEpoch();

/**
 * @brief Set time from Unix epoch
 * 
 * @param epoch Unix timestamp
 * @return true if set successful
 */
bool setEpoch(time_t epoch);

/**
 * @brief Configure FOUT pin frequency
 * 
 * @param freq Desired output frequency
 * @return true if configuration successful
 */
bool setFOUT(FOUTFreq freq);

/**
 * @brief Enable 1Hz output on FOUT pin
 * 
 * Convenience function for timestamp discipline.
 * 
 * @return true if configuration successful
 */
bool enableFOUT1Hz();

/**
 * @brief Disable FOUT output
 * 
 * @return true if configuration successful
 */
bool disableFOUT();

/**
 * @brief Read temperature from TCXO
 * 
 * Resolution: 0.25°C
 * Range: -40°C to +85°C
 * 
 * @return Temperature in degrees Celsius (or -128 on error)
 */
float getTemperature();

/**
 * @brief Clear all flags in FLAG register
 * 
 * @return true if successful
 */
bool clearFlags();

/**
 * @brief Read raw register value
 * 
 * @param reg Register address
 * @param value Pointer to store value
 * @return true if read successful
 */
bool readRegister(uint8_t reg, uint8_t* value);

/**
 * @brief Write raw register value
 * 
 * @param reg Register address
 * @param value Value to write
 * @return true if write successful
 */
bool writeRegister(uint8_t reg, uint8_t value);

// ============================================================================
// Compile-Time Sync Functions
// ============================================================================

/**
 * @brief Get firmware compile time as Unix epoch
 * 
 * Parses __DATE__ and __TIME__ macros from compilation.
 * 
 * @return Unix timestamp of compile time (0 on parse error)
 */
time_t getCompileEpoch();

/**
 * @brief Sync RTC to firmware compile time
 * 
 * Sets the RTC to the firmware build timestamp.
 * Useful for initial time sync when no other source is available.
 * 
 * @return true if sync successful
 */
bool syncToCompileTime();

/**
 * @brief Check if RTC needs time sync
 * 
 * Returns true if:
 * - VLF flag is set (time lost due to low battery)
 * - Year < 2024 (factory default / uninitialized)
 * - Current time is older than compile time
 * 
 * @return true if sync is recommended
 */
bool needsTimeSync();

/**
 * @brief Format time as string (YYYY-MM-DD HH:MM:SS)
 * 
 * @param epoch Unix timestamp
 * @param buf Buffer to write to (at least 20 chars)
 * @param bufLen Buffer length
 */
void formatTime(time_t epoch, char* buf, size_t bufLen);

} // namespace RX8900CE

#endif // RX8900CE_H

