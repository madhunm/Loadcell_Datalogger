# Loadcell Datalogger - User Manual

**Version:** 1.0  
**Date:** December 2024  
**Hardware:** ESP32-S3 DevKitC1 with MAX11270 ADC, LSM6DSV16X IMU, RX8900CE RTC

---

## Table of Contents

1. [Introduction](#introduction)
2. [System Overview](#system-overview)
3. [Hardware Setup](#hardware-setup)
4. [Initial Power-On](#initial-power-on)
5. [Operation](#operation)
6. [NeoPixel Status Indicators](#neopixel-status-indicators)
7. [Web Configuration Portal](#web-configuration-portal)
8. [Data Files](#data-files)
9. [Troubleshooting](#troubleshooting)
10. [Specifications](#specifications)

---

## 1. Introduction

The Loadcell Datalogger is a high-speed data acquisition system designed for capturing force and acceleration data from load cells during dynamic events. The system can record:

- **ADC Data:** 24-bit load cell measurements at up to 64,000 samples per second (64 ksps)
- **IMU Data:** 6-axis accelerometer and gyroscope data at up to 960 Hz

Data is stored on a removable SD card in binary format for maximum speed, then converted to CSV format for analysis.

---

## 2. System Overview

### 2.1 Key Features

- **High-Speed Sampling:** 64 ksps ADC, 960 Hz IMU
- **Dual-Core Processing:** Dedicated core for sampling, separate core for logging
- **Real-Time Clock:** Timestamped data with RTC synchronization
- **Visual Feedback:** NeoPixel LED provides system status and error indication
- **Web Configuration:** WiFi access point for remote configuration
- **Data Integrity:** CRC32 checksums verify data integrity
- **Fault Tolerance:** Automatic error detection and recovery

### 2.2 System Architecture

```
┌─────────────────────────────────────────┐
│         ESP32-S3 DevKitC1              │
│                                         │
│  Core 0: Sampling Tasks                │
│    ├─ ADC Sampling (64 ksps)          │
│    └─ IMU Sampling (960 Hz)            │
│                                         │
│  Core 1: Main Loop                     │
│    ├─ Data Logging                     │
│    ├─ Web Server                       │
│    ├─ Button Handling                  │
│    └─ System State Machine             │
│                                         │
│  Peripherals:                          │
│    ├─ MAX11270 ADC (SPI)               │
│    ├─ LSM6DSV16X IMU (I2C)            │
│    ├─ RX8900CE RTC (I2C)              │
│    ├─ SD Card (SDMMC 4-bit)           │
│    └─ NeoPixel LED                     │
└─────────────────────────────────────────┘
```

---

## 3. Hardware Setup

### 3.1 Required Components

- ESP32-S3 DevKitC1 development board
- MAX11270 24-bit ADC module
- LSM6DSV16X IMU module
- RX8900CE RTC module
- SD card (Class 10 or better, formatted as FAT32)
- Load cell with appropriate signal conditioning
- Power supply (5V USB or external)
- LOG_START button (connects to GPIO 2)

### 3.2 Connections

Refer to `include/pins.h` for complete pin assignments:

**SD Card (SDMMC 4-bit):**
- CLK: GPIO 4
- CMD: GPIO 5
- D0-D3: GPIO 6-9
- CD (Card Detect): GPIO 10

**ADC (MAX11270 SPI):**
- MISO: GPIO 12
- MOSI: GPIO 13
- SYNC: GPIO 14
- RSTB: GPIO 15
- RDYB: GPIO 16
- CS: GPIO 17
- SCK: GPIO 18

**IMU (LSM6DSV16X I2C):**
- SDA: GPIO 41
- SCL: GPIO 42
- INT1: GPIO 39
- INT2: GPIO 40

**RTC (RX8900CE I2C):**
- SDA: GPIO 41 (shared with IMU)
- SCL: GPIO 42 (shared with IMU)
- FOUT: GPIO 33
- INT: GPIO 34

**NeoPixel:**
- Data: GPIO 21

**Button:**
- LOG_START: GPIO 2 (with external pulldown to GND)

### 3.3 SD Card Preparation

1. Format SD card as FAT32 (use SD Formatter tool or Windows format)
2. Ensure card is Class 10 or better for reliable high-speed writes
3. Recommended capacity: 8GB to 32GB
4. Insert card before power-on

---

## 4. Initial Power-On

### 4.1 First Boot Sequence

1. **Power On:** Connect USB cable or external 5V supply
2. **Watch NeoPixel:** LED will show initialization pattern (blue, solid)
3. **Check Serial Output:** Open serial monitor at 115200 baud to view initialization messages
4. **Wait for Ready:** System transitions to READY state (breathing green LED)

### 4.2 Initialization States

The system goes through the following initialization sequence:

1. **GPIO Initialization:** All pins configured
2. **SD Card Mount:** SD card detected and mounted
3. **IMU Initialization:** Accelerometer and gyroscope configured
4. **RTC Initialization:** Real-time clock synchronized
5. **ADC Initialization:** ADC calibrated and started
6. **Web Server Start:** WiFi access point created (if enabled)

If any step fails, the NeoPixel will show a specific error pattern (see Section 6).

### 4.3 WiFi Access Point

On first boot, the system creates a WiFi access point with SSID:
```
Loadcell_Datalogger_XXXX
```
Where `XXXX` is a random 4-digit number. This SSID is stored in non-volatile memory and will persist across reboots.

**Default IP Address:** 192.168.4.1

Connect to this network (no password) and open a web browser to:
```
http://192.168.4.1
```

---

## 5. Operation

### 5.1 Starting a Logging Session

1. **Ensure System is Ready:** NeoPixel should show breathing green pattern
2. **Press LOG_START Button:** Momentarily press the button
3. **Logging Starts:** NeoPixel changes to solid green
4. **Data Collection:** System begins recording ADC and IMU data to SD card

### 5.2 During Logging

- **NeoPixel:** Solid green indicates active logging
- **SD Card:** Do NOT remove during logging
- **Data Rate:** Approximately 1-2 MB per minute (depends on sample rates)
- **Duration:** Logging continues until button is pressed again

### 5.3 Stopping a Logging Session

1. **Press LOG_START Button Again:** Momentarily press the button
2. **Logging Stops:** System stops recording and begins conversion
3. **Conversion Phase:** NeoPixel shows yellow/amber blinking pattern
   - **DO NOT REMOVE SD CARD** during this phase
4. **Conversion Complete:** NeoPixel shows green double-blink pattern
   - **Safe to Remove:** SD card can now be safely removed

### 5.4 Data Conversion

After stopping a logging session, the system automatically converts binary log files to CSV format:

- **Input Files:** `YYYYMMDD_HHMMSS_ADC.bin` and `YYYYMMDD_HHMMSS_IMU.bin`
- **Output File:** `YYYYMMDD_HHMMSS.csv`
- **Duration:** Typically 10-30 seconds depending on session length
- **Status:** Check NeoPixel pattern (see Section 6)

---

## 6. NeoPixel Status Indicators

The NeoPixel LED provides visual feedback about system status. All patterns follow ANSI Z535.1 / IEC 60073 standards for safety and status indication.

### 6.1 Normal Operation Patterns

| Pattern | Color | Description |
|---------|-------|-------------|
| **INIT** | Blue (Solid) | System initializing / peripherals starting up |
| **READY** | Green (Breathing) | System ready to start logging |
| **LOGGING** | Green (Solid) | Actively logging data to SD card |
| **CONVERTING** | Yellow/Amber (Slow Blink) | Converting binary logs to CSV (DO NOT remove SD card) |
| **SAFE TO REMOVE** | Green (Double Blink) | CSV conversion complete (safe to remove SD card) |

### 6.2 Error Patterns

| Pattern | Color | Description |
|---------|-------|-------------|
| **ERROR: SD Card** | Red (Double Blink) | SD card initialization or access error |
| **ERROR: RTC** | Yellow/Amber (Single Blink) | Real-time clock initialization error |
| **ERROR: IMU** | Red (Triple Blink) | IMU (accelerometer/gyroscope) initialization error |
| **ERROR: ADC** | Red (Double Blink) | ADC (loadcell) initialization error |
| **ERROR: Write Failure** | Red (Fast Blink) | Persistent SD card write failures (critical) |
| **ERROR: Low Space** | Yellow/Amber (Double Blink) | SD card running low on free space |
| **ERROR: Buffer Full** | Red (Triple Blink) | ADC or IMU ring buffer overflow |

### 6.3 Pattern Timing Details

- **Breathing:** 2-second period, brightness varies from 30 to 255
- **Slow Blink:** 250ms on, 250ms off
- **Double Blink:** 100ms on, 100ms off, 2 pulses, 800ms gap
- **Triple Blink:** 100ms on, 100ms off, 3 pulses, 600ms gap
- **Fast Blink:** 50ms on, 50ms off

---

## 7. Web Configuration Portal

### 7.1 Accessing the Portal

1. Connect to WiFi network: `Loadcell_Datalogger_XXXX`
2. Open web browser to: `http://192.168.4.1`
3. Web interface loads automatically

### 7.2 Main Portal Features

The main web portal has three tabs:

#### **Status Tab**
- System status indicators
- SD card storage information (progress bar)
- Write failure statistics
- Logger state
- Auto-refresh capability

#### **Data Visualization Tab**
- Interactive chart with dual Y-axes
  - Left axis: Force (N) from ADC
  - Right axis: Acceleration (g) from IMU
- Moving average filter (configurable window)
- File comparison (overlay multiple CSV files)
- Peak statistics (max force and deceleration)
- Auto-refresh with configurable interval

#### **Help & User Guide Tab**
- Usage instructions
- Animated NeoPixel pattern reference
- System information

### 7.3 Calibration Portal (Hidden)

**Access:** Navigate to `http://192.168.4.1/cal`

**Warning:** This portal is for calibration and advanced configuration only. Do not share this URL with end users.

**Features:**
- Loadcell calibration (scaling factor and offset)
- ADC settings (sample rate, PGA gain)
- IMU settings (sample rate, ranges)
- Save/Load configuration

**Calibration Values:**
- **Scaling Factor:** Converts ADC counts to Newtons (N)
  - Formula: `Force (N) = (ADC_Code - Offset) × Scale`
- **ADC Baseline/Offset:** ADC value corresponding to 0N
  - Typically 8,388,608 for 24-bit mid-point

---

## 8. Data Files

### 8.1 File Naming Convention

Files are named using timestamp format:
```
YYYYMMDD_HHMMSS
```

Examples:
- `20241204_153012_ADC.bin` - ADC binary log
- `20241204_153012_IMU.bin` - IMU binary log
- `20241204_153012.csv` - Combined CSV output

### 8.2 Binary File Format

**ADC Binary File (.bin):**
- Header: 64 bytes (magic, version, config, RTC time, CRC32)
- Records: 8 bytes each (index: 4 bytes, code: 4 bytes)
- Little-endian format

**IMU Binary File (.bin):**
- Header: 64 bytes (magic, version, config, RTC time, CRC32)
- Records: 32 bytes each (index: 4, adcIndex: 4, ax/ay/az: 12, gx/gy/gz: 12)
- Little-endian format

### 8.3 CSV File Format

The CSV file contains aligned data with the following columns:

```
ADC_Index,Time_Seconds,ADC_Code,IMU_Index,AX,AY,AZ,GX,GY,GZ
```

- **ADC_Index:** Monotonically increasing sample index
- **Time_Seconds:** Time in seconds from session start
- **ADC_Code:** Raw 24-bit ADC value
- **IMU_Index:** IMU sample index
- **AX, AY, AZ:** Accelerometer values in g
- **GX, GY, GZ:** Gyroscope values in degrees per second (dps)

**Data Alignment:**
- IMU samples are forward-filled to match ADC sample rate
- Each ADC sample has corresponding IMU data (interpolated if needed)

### 8.4 Data Integrity

- **CRC32 Checksums:** Both binary files include CRC32 checksums in headers
- **Verification:** CSV conversion verifies CRC32 and logs warnings on mismatch
- **File Size:** Check file sizes match expected values based on session duration

---

## 9. Troubleshooting

### 9.1 Common Issues

#### **SD Card Not Detected**
- **Symptom:** Red double-blink pattern, "SD card initialisation failed" message
- **Solutions:**
  - Check SD card is properly inserted
  - Verify card is formatted as FAT32
  - Try a different SD card (Class 10 or better)
  - Check card detect pin connection (GPIO 10)

#### **ADC Initialization Failed**
- **Symptom:** Red double-blink pattern, "adcInit() failed" message
- **Solutions:**
  - Check SPI connections (MISO, MOSI, SCK, CS)
  - Verify SYNC and RSTB pins are connected
  - Check RDYB pin connection
  - Review serial output for specific error codes

#### **IMU Initialization Failed**
- **Symptom:** Red triple-blink pattern, "IMU initialisation failed" message
- **Solutions:**
  - Check I2C connections (SDA, SCL)
  - Verify I2C pull-up resistors (typically 4.7kΩ)
  - Check power supply to IMU module
  - Review serial output for I2C errors

#### **RTC Initialization Failed**
- **Symptom:** Yellow/amber single-blink pattern, "RTC initialisation failed" message
- **Solutions:**
  - Check I2C connections (shared with IMU)
  - Verify RTC module power supply
  - Check FOUT and INT pin connections

#### **Write Failures**
- **Symptom:** Red fast-blink pattern, write failure statistics in web portal
- **Solutions:**
  - Check SD card write speed (use Class 10 or better)
  - Verify SD card has sufficient free space
  - Check for card removal during logging
  - Try a different SD card

#### **Buffer Overflow**
- **Symptom:** Red triple-blink pattern, "Buffer overflow" message
- **Solutions:**
  - Reduce sample rates if possible
  - Check SD card write performance
  - Ensure logging task is running on Core 1
  - Review system load

#### **Low SD Card Space**
- **Symptom:** Yellow/amber double-blink pattern, warning in web portal
- **Solutions:**
  - Delete old log files from SD card
  - Use larger capacity SD card
  - Reduce logging session duration

### 9.2 Serial Debug Output

Enable debug logging by setting in `platformio.ini`:
```ini
build_flags = -D LOGGER_DEBUG=1
```

Debug output includes:
- Detailed initialization steps
- Buffer status
- Write statistics
- Error codes and diagnostics

### 9.3 Web Portal Diagnostics

The web portal status tab provides:
- Real-time system status
- Buffer levels
- Write statistics
- SD card space information
- Memory usage

---

## 10. Specifications

### 10.1 Performance

| Parameter | Specification |
|-----------|---------------|
| **ADC Sample Rate** | 1,000 - 64,000 Hz (configurable) |
| **ADC Resolution** | 24-bit signed |
| **ADC PGA Gain** | x1, x2, x4, x8, x16, x32, x64, x128 |
| **IMU Sample Rate** | 15 - 960 Hz (configurable) |
| **IMU Accelerometer Range** | ±2g, ±4g, ±8g, ±16g |
| **IMU Gyroscope Range** | ±125, ±250, ±500, ±1000, ±2000 dps |
| **Data Rate** | ~1-2 MB/minute (64 ksps ADC, 960 Hz IMU) |
| **Max Session Duration** | Limited by SD card capacity |

### 10.2 Storage

| Parameter | Specification |
|-----------|---------------|
| **SD Card Format** | FAT32 |
| **SD Card Interface** | SDMMC 4-bit |
| **Recommended Card** | Class 10 or better, 8-32 GB |
| **File Location** | `/log/` directory on SD card |

### 10.3 Power

| Parameter | Specification |
|-----------|---------------|
| **Input Voltage** | 5V (USB) or external 5V supply |
| **Current Consumption** | ~200-300 mA (typical) |
| **Peak Current** | ~500 mA (during writes) |

### 10.4 Environmental

| Parameter | Specification |
|-----------|---------------|
| **Operating Temperature** | 0°C to 70°C (component dependent) |
| **Storage Temperature** | -20°C to 85°C |
| **Humidity** | 10% to 90% RH (non-condensing) |

---

## Appendix A: File Structure

```
/log/
├── 20241204_120000_ADC.bin    (Binary ADC data)
├── 20241204_120000_IMU.bin    (Binary IMU data)
├── 20241204_120000.csv        (Combined CSV output)
├── 20241204_130000_ADC.bin
├── 20241204_130000_IMU.bin
└── 20241204_130000.csv
```

---

## Appendix B: Quick Reference

### Button Operations

| Action | Button Press | Result |
|--------|--------------|--------|
| Start Logging | Press once (READY state) | Begins data collection |
| Stop Logging | Press once (LOGGING state) | Stops collection, starts conversion |
| Ignored | Press (CONVERTING state) | No action (conversion in progress) |

### NeoPixel Quick Reference

- **Blue Solid:** Initializing
- **Green Breathing:** Ready
- **Green Solid:** Logging
- **Yellow Blinking:** Converting (DO NOT remove SD card)
- **Green Double Blink:** Safe to remove SD card
- **Red Patterns:** Error (see Section 6.2)

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | December 2024 | Initial release |

---

**End of User Manual**

