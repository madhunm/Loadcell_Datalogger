# Loadcell Datalogger - Bring-Up, Test, and Calibration Plan

**Version:** 1.0  
**Date:** December 2024  
**Hardware:** ESP32-S3 DevKitC1 with MAX11270 ADC, LSM6DSV16X IMU, RX8900CE RTC

---

## Table of Contents

1. [Overview](#overview)
2. [Pre-Assembly Checklist](#pre-assembly-checklist)
3. [Hardware Assembly](#hardware-assembly)
4. [Initial Bring-Up](#initial-bring-up)
5. [Functional Testing](#functional-testing)
6. [Performance Testing](#performance-testing)
7. [Calibration Procedure](#calibration-procedure)
8. [System Validation](#system-validation)
9. [Troubleshooting Guide](#troubleshooting-guide)
10. [Acceptance Criteria](#acceptance-criteria)

---

## 1. Overview

This document provides a step-by-step procedure for bringing up, testing, and calibrating the Loadcell Datalogger system. Follow these procedures in order to ensure proper system operation.

### 1.1 Objectives

- Verify all hardware connections
- Validate firmware functionality
- Test data acquisition performance
- Calibrate loadcell scaling and offset
- Validate data integrity and accuracy

### 1.2 Prerequisites

- Assembled hardware (PCB or breadboard)
- Firmware flashed to ESP32-S3
- Serial monitor access (115200 baud)
- SD card (Class 10, FAT32 formatted)
- Known reference loadcell (for calibration)
- Test weights or force calibration rig
- Multimeter (for voltage measurements)
- Oscilloscope (optional, for signal verification)

---

## 2. Pre-Assembly Checklist

### 2.1 Components Verification

- [ ] ESP32-S3 DevKitC1 board
- [ ] MAX11270 ADC module/breakout
- [ ] LSM6DSV16X IMU module/breakout
- [ ] RX8900CE RTC module/breakout
- [ ] SD card socket with card detect
- [ ] NeoPixel LED (WS2812B or compatible)
- [ ] LOG_START button (momentary, NO)
- [ ] Load cell with signal conditioning
- [ ] Power supply (5V, 1A minimum)
- [ ] I2C pull-up resistors (4.7kΩ, 2x)
- [ ] SPI pull-up resistors (if required)
- [ ] Jumper wires or PCB connections

### 2.2 Software Preparation

- [ ] PlatformIO installed and configured
- [ ] ESP32-S3 toolchain installed
- [ ] Firmware source code available
- [ ] Serial monitor software (Arduino IDE, PuTTY, or similar)
- [ ] SD card formatter tool
- [ ] CSV viewer/editor (Excel, LibreOffice, etc.)

### 2.3 Documentation Review

- [ ] Pin assignment diagram (`include/pins.h`)
- [ ] Schematic review (if available)
- [ ] User manual review
- [ ] This bring-up plan

---

## 3. Hardware Assembly

### 3.1 Power Supply Connections

1. **Verify Power Supply:**
   - Output: 5V ±5%
   - Current capacity: ≥1A
   - Ripple: <50mV

2. **Connect Power:**
   - Connect 5V to ESP32-S3 VIN or USB
   - Connect GND to common ground
   - **DO NOT POWER ON YET**

### 3.2 SD Card Interface

1. **SD Card Socket Connections:**
   ```
   CLK  → GPIO 4
   CMD  → GPIO 5
   D0   → GPIO 6
   D1   → GPIO 7
   D2   → GPIO 8
   D3   → GPIO 9
   CD   → GPIO 10 (card detect, active LOW)
   VCC  → 3.3V
   GND  → GND
   ```

2. **Verify Connections:**
   - Use continuity tester
   - Check for shorts to adjacent pins
   - Verify card detect pull-up (typically 10kΩ to 3.3V)

### 3.3 ADC (MAX11270) Connections

1. **SPI Connections:**
   ```
   MISO → GPIO 12
   MOSI → GPIO 13
   SCK  → GPIO 18
   CS   → GPIO 17
   SYNC → GPIO 14
   RSTB → GPIO 15
   RDYB → GPIO 16 (active LOW)
   ```

2. **Power Connections:**
   - AVDD: 3.3V or 5V (check MAX11270 datasheet)
   - DVDD: 3.3V
   - GND: Common ground

3. **Load Cell Connections:**
   - AINP → Load cell positive output
   - AINN → Load cell negative output
   - AGND → Load cell ground

### 3.4 IMU (LSM6DSV16X) Connections

1. **I2C Connections:**
   ```
   SDA → GPIO 41
   SCL → GPIO 42
   ```

2. **Interrupt Pins (Optional):**
   ```
   INT1 → GPIO 39
   INT2 → GPIO 40
   ```

3. **Power:**
   - VDD: 3.3V
   - GND: Common ground

4. **Pull-Up Resistors:**
   - SDA: 4.7kΩ to 3.3V
   - SCL: 4.7kΩ to 3.3V

### 3.5 RTC (RX8900CE) Connections

1. **I2C Connections (Shared with IMU):**
   ```
   SDA → GPIO 41 (shared)
   SCL → GPIO 42 (shared)
   ```

2. **Additional Pins:**
   ```
   FOUT → GPIO 33 (1 Hz output, optional)
   INT  → GPIO 34 (interrupt, active LOW)
   ```

3. **Power:**
   - VDD: 3.3V
   - GND: Common ground

### 3.6 NeoPixel LED

1. **Connection:**
   ```
   Data → GPIO 21
   VCC  → 5V (or 3.3V if 3.3V logic version)
   GND  → GND
   ```

2. **Note:** If using 5V NeoPixel, ensure GPIO 21 is 5V tolerant or use level shifter

### 3.7 LOG_START Button

1. **Connection:**
   ```
   One terminal → GPIO 2
   Other terminal → 3.3V (or use internal pull-down)
   ```

2. **External Pull-Down:**
   - Add 10kΩ resistor from GPIO 2 to GND
   - Button connects GPIO 2 to 3.3V when pressed

### 3.8 Final Assembly Checks

- [ ] All connections verified with continuity tester
- [ ] No shorts between power and ground
- [ ] I2C pull-ups installed
- [ ] SD card inserted (formatted as FAT32)
- [ ] Load cell connected (if available)
- [ ] Power supply verified (5V, sufficient current)

---

## 4. Initial Bring-Up

### 4.1 First Power-On

1. **Connect Serial Monitor:**
   - Baud rate: 115200
   - Data bits: 8
   - Parity: None
   - Stop bits: 1
   - Flow control: None

2. **Apply Power:**
   - Connect USB cable or external 5V supply
   - Observe NeoPixel LED

3. **Expected Behavior:**
   - NeoPixel: Blue solid (INIT pattern)
   - Serial output: Initialization messages
   - Transition to green breathing (READY pattern)

### 4.2 Serial Output Verification

Expected serial output sequence:

```
Loadcell Logger – ESP32-S3 bring-up
------------------------------------
[INIT] Watchdog timer initialized (5s timeout)
GPIO initialized. I2C started on SDA=41 SCL=42
[INIT] SD card initialisation...
[INIT] IMU initialisation...
[INIT] RTC initialisation...
[INIT][ADC] adcInit()...
[STATE] READY: system ready to log (waiting for Logstart button)
```

### 4.3 Error Detection

If initialization fails, check:

1. **SD Card Error (Red Double Blink):**
   - Verify SD card is inserted
   - Check card detect pin (GPIO 10)
   - Verify SD card format (FAT32)
   - Check connections (CLK, CMD, D0-D3)

2. **IMU Error (Red Triple Blink):**
   - Check I2C connections (SDA, SCL)
   - Verify pull-up resistors (4.7kΩ)
   - Check power supply to IMU
   - Review I2C address (default 0x6A)

3. **RTC Error (Yellow Single Blink):**
   - Check I2C connections (shared with IMU)
   - Verify RTC module power
   - Check FOUT and INT pins

4. **ADC Error (Red Double Blink):**
   - Check SPI connections
   - Verify SYNC and RSTB pins
   - Check RDYB pin connection
   - Review serial output for specific error codes

### 4.4 WiFi Access Point Verification

1. **Check for AP:**
   - Look for WiFi network: `Loadcell_Datalogger_XXXX`
   - Connect to network (no password)

2. **Access Web Portal:**
   - Open browser to: `http://192.168.4.1`
   - Verify web interface loads

3. **Check Status Tab:**
   - SD card status: Should show "✓ OK"
   - SD card storage: Should show free space
   - Logger state: Should show "IDLE"

---

## 5. Functional Testing

### 5.1 Button Functionality Test

1. **Test Button Press Detection:**
   - Press LOG_START button
   - Verify serial output: `[BUTTON] Logstart pressed.`
   - Verify NeoPixel changes to solid green (LOGGING)

2. **Test Logging Start:**
   - Verify serial output: `[STATE] LOGGING: logging started.`
   - Check web portal: Logger state should show "SESSION_OPEN"

3. **Test Logging Stop:**
   - Press button again
   - Verify NeoPixel changes to yellow blinking (CONVERTING)
   - Verify serial output: `[STATE] CONVERTING: logging stopped, starting CSV conversion...`

4. **Test Conversion Complete:**
   - Wait for conversion (10-30 seconds)
   - Verify NeoPixel changes to green double-blink (SAFE_TO_REMOVE)
   - Verify serial output: `[STATE] READY: CSV conversion complete. Safe to remove SD card.`

### 5.2 SD Card Write Test

1. **Start Short Logging Session:**
   - Press LOG_START button
   - Wait 10 seconds
   - Press button again to stop

2. **Verify Files Created:**
   - Remove SD card (after SAFE_TO_REMOVE pattern)
   - Insert into computer
   - Check `/log/` directory for:
     - `YYYYMMDD_HHMMSS_ADC.bin`
     - `YYYYMMDD_HHMMSS_IMU.bin`
     - `YYYYMMDD_HHMMSS.csv`

3. **Verify File Sizes:**
   - ADC.bin: Should be ~64 bytes (header) + (10s × 64000 samples/s × 8 bytes) ≈ 5.1 MB
   - IMU.bin: Should be ~64 bytes (header) + (10s × 960 samples/s × 32 bytes) ≈ 307 KB
   - CSV: Should be similar size to combined binary files

### 5.3 Data Integrity Test

1. **Check CSV File:**
   - Open CSV file in spreadsheet software
   - Verify columns: ADC_Index, Time_Seconds, ADC_Code, IMU_Index, AX, AY, AZ, GX, GY, GZ
   - Verify data is present and reasonable

2. **Check Time Alignment:**
   - Time_Seconds should increase monotonically
   - IMU samples should align with ADC samples
   - No large gaps in time sequence

3. **Check CRC32 (if implemented):**
   - Review serial output for CRC32 verification messages
   - Verify no CRC32 mismatch warnings

### 5.4 Web Portal Functionality Test

1. **Status Tab:**
   - Verify all status indicators update
   - Check SD card space display
   - Verify write statistics (should show 0 failures for successful session)

2. **Data Visualization Tab:**
   - Verify chart loads
   - Check that latest CSV file is displayed
   - Test moving average filter
   - Test file comparison feature
   - Verify peak statistics display

3. **Help Tab:**
   - Verify NeoPixel patterns are displayed
   - Check animations work

4. **Calibration Portal:**
   - Navigate to `http://192.168.4.1/cal`
   - Verify calibration form loads
   - Test save/load functionality

---

## 6. Performance Testing

### 6.1 ADC Sampling Rate Test

1. **Configure Sample Rate:**
   - Use web portal or default (64 ksps)
   - Start logging session
   - Log for 60 seconds

2. **Verify Sample Count:**
   - Check CSV file: ADC_Index should reach ~3,840,000 (60s × 64000)
   - Verify no missing samples (check for gaps in ADC_Index)

3. **Check Timing:**
   - Time_Seconds should reach ~60.0 seconds
   - Calculate actual sample rate: `max(ADC_Index) / max(Time_Seconds)`
   - Should be within 1% of configured rate

### 6.2 IMU Sampling Rate Test

1. **Configure IMU ODR:**
   - Use web portal or default (960 Hz)
   - Start logging session
   - Log for 60 seconds

2. **Verify Sample Count:**
   - Check CSV file: IMU_Index should reach ~57,600 (60s × 960)
   - Verify IMU samples are present for each ADC sample

3. **Check Alignment:**
   - Verify adcSampleIndex in IMU records matches ADC_Index
   - No large misalignments

### 6.3 Buffer Performance Test

1. **Monitor Buffer Levels:**
   - Use web portal status tab
   - During logging, check buffer levels
   - Should remain below 80% capacity

2. **Check for Overflows:**
   - Review serial output for overflow messages
   - Check web portal for buffer overflow indicators
   - Verify no data loss

### 6.4 Write Performance Test

1. **Long Duration Test:**
   - Start logging session
   - Log for 10 minutes
   - Monitor write statistics in web portal

2. **Verify No Write Failures:**
   - Check write failure count (should be 0)
   - Verify no consecutive failures
   - Check SD card space decreases appropriately

3. **Check File Integrity:**
   - After conversion, verify CSV file is complete
   - Check for missing data or corruption

### 6.5 System Stability Test

1. **Extended Operation:**
   - Run system for 1 hour
   - Perform multiple start/stop cycles
   - Monitor for memory leaks or crashes

2. **Watchdog Test:**
   - System should not reset unexpectedly
   - Watchdog should only trigger on actual hangs

---

## 7. Calibration Procedure

### 7.1 Loadcell Calibration Overview

The loadcell requires two calibration parameters:
- **Scaling Factor:** Converts ADC counts to Newtons (N)
- **Offset/Baseline:** ADC value corresponding to 0N

### 7.2 Calibration Setup

1. **Required Equipment:**
   - Known reference weights or force calibration rig
   - Stable mounting for loadcell
   - Means to apply known forces (0N, 10N, 20N, etc.)

2. **Preparation:**
   - Mount loadcell securely
   - Ensure no external forces when unloaded
   - Allow system to stabilize (5 minutes warm-up)

### 7.3 Zero Force Calibration

1. **Apply Zero Force:**
   - Ensure loadcell is unloaded
   - No external forces applied

2. **Record ADC Value:**
   - Start short logging session (10 seconds)
   - Stop and convert to CSV
   - Calculate average ADC_Code value
   - This is the **Offset/Baseline**

3. **Expected Value:**
   - For 24-bit signed ADC: typically 8,388,608 (2^23)
   - May vary based on loadcell and signal conditioning

### 7.4 Force Calibration

1. **Apply Known Forces:**
   - Apply reference weights or known forces
   - Recommended points: 0N, 10N, 20N, 50N, 100N (or full scale)
   - Allow system to stabilize at each point

2. **Record Data:**
   - Log 10 seconds at each force level
   - Calculate average ADC_Code for each force

3. **Calculate Scaling Factor:**
   - Use linear regression or two-point method:
     ```
     Scaling_Factor = (Force2 - Force1) / (ADC2 - ADC1)
     ```
   - Example: If 20kN = +3,000,000 ADC counts above baseline:
     ```
     Scaling_Factor = 20000 N / 3000000 counts = 0.00667 N/ADC
     ```

### 7.5 Calibration Validation

1. **Apply Test Forces:**
   - Apply forces not used in calibration
   - Verify calculated forces match applied forces
   - Error should be <2% of full scale

2. **Check Linearity:**
   - Plot Force vs ADC_Code
   - Should be linear (R² > 0.99)
   - Check for hysteresis

### 7.6 Entering Calibration Values

1. **Access Calibration Portal:**
   - Navigate to `http://192.168.4.1/cal`
   - Enter scaling factor (e.g., 0.00667)
   - Enter offset/baseline (e.g., 8388608)

2. **Save Configuration:**
   - Click "Save All Settings"
   - Verify success message
   - Values are stored in NVS (non-volatile storage)

3. **Verify in Main Portal:**
   - Check Data Visualization tab
   - Force values should now display in Newtons
   - Verify against known test forces

### 7.7 Calibration Documentation

Record calibration data:

| Force (N) | ADC Code | Notes |
|-----------|----------|-------|
| 0 | | |
| 10 | | |
| 20 | | |
| 50 | | |
| 100 | | |

**Calculated Values:**
- Scaling Factor: ________ N/ADC
- Offset/Baseline: ________ ADC counts
- Date: ________
- Calibrated by: ________

---

## 8. System Validation

### 8.1 End-to-End Test

1. **Full System Test:**
   - Start logging session
   - Apply known force profile
   - Stop logging
   - Verify conversion completes
   - Analyze CSV data

2. **Force Profile Verification:**
   - Compare recorded forces to applied forces
   - Check timing accuracy
   - Verify no data loss

### 8.2 Accuracy Validation

1. **Static Accuracy:**
   - Apply known static forces
   - Verify recorded forces within ±2% of applied
   - Check for drift over time

2. **Dynamic Accuracy:**
   - Apply known dynamic force profile
   - Verify peak forces are captured
   - Check timing of events

### 8.3 Data Quality Validation

1. **Signal Quality:**
   - Check for excessive noise
   - Verify signal-to-noise ratio
   - Check for artifacts or glitches

2. **Alignment Validation:**
   - Verify ADC and IMU data are properly aligned
   - Check time synchronization
   - Verify no sample drops

---

## 9. Troubleshooting Guide

### 9.1 Initialization Failures

**SD Card:**
- Check card format (must be FAT32)
- Verify card detect pin (GPIO 10)
- Try different SD card
- Check connections (CLK, CMD, D0-D3)

**IMU:**
- Verify I2C address (scan bus)
- Check pull-up resistors
- Verify power supply
- Check SDA/SCL connections

**RTC:**
- Check I2C bus (shared with IMU)
- Verify RTC module power
- Check FOUT/INT pins

**ADC:**
- Verify SPI connections
- Check SYNC/RSTB/RDYB pins
- Review MAX11270 datasheet
- Check power supplies (AVDD, DVDD)

### 9.2 Data Acquisition Issues

**Missing Samples:**
- Check buffer sizes
- Verify sampling tasks are running
- Check CPU load
- Review FreeRTOS task priorities

**Incorrect Values:**
- Verify calibration values
- Check loadcell connections
- Verify signal conditioning
- Check for electrical interference

**Write Failures:**
- Use Class 10 or better SD card
- Check SD card connections
- Verify sufficient free space
- Check for card removal during logging

### 9.3 Performance Issues

**Slow Conversion:**
- Normal for large files
- Check SD card read speed
- Verify FreeRTOS task is running
- Check system load

**Buffer Overflows:**
- Reduce sample rates
- Check SD card write speed
- Verify logging task priority
- Check for system hangs

---

## 10. Acceptance Criteria

### 10.1 Functional Requirements

- [ ] All peripherals initialize successfully
- [ ] Button starts/stops logging correctly
- [ ] Data files are created on SD card
- [ ] CSV conversion completes successfully
- [ ] Web portal is accessible and functional
- [ ] NeoPixel patterns indicate correct states

### 10.2 Performance Requirements

- [ ] ADC samples at configured rate (within 1%)
- [ ] IMU samples at configured rate (within 1%)
- [ ] No buffer overflows during normal operation
- [ ] No write failures during normal operation
- [ ] Conversion completes within 30 seconds for 10-minute session

### 10.3 Accuracy Requirements

- [ ] Force measurements within ±2% of applied force
- [ ] Timing accuracy within ±0.1ms
- [ ] No missing samples
- [ ] Data alignment correct (ADC and IMU)

### 10.4 Reliability Requirements

- [ ] System operates for 1 hour without crashes
- [ ] Multiple start/stop cycles complete successfully
- [ ] SD card removal detected and handled gracefully
- [ ] Watchdog prevents system hangs

---

## Appendix A: Test Record Template

**Test Date:** ________  
**Tester:** ________  
**Firmware Version:** ________  
**Hardware Revision:** ________

### Initialization Test
- [ ] SD Card: PASS / FAIL
- [ ] IMU: PASS / FAIL
- [ ] RTC: PASS / FAIL
- [ ] ADC: PASS / FAIL
- [ ] Web Portal: PASS / FAIL

### Functional Test
- [ ] Button Start: PASS / FAIL
- [ ] Button Stop: PASS / FAIL
- [ ] File Creation: PASS / FAIL
- [ ] CSV Conversion: PASS / FAIL

### Performance Test
- [ ] ADC Rate: ________ Hz (Target: ________ Hz)
- [ ] IMU Rate: ________ Hz (Target: ________ Hz)
- [ ] Buffer Overflows: ________
- [ ] Write Failures: ________

### Calibration
- [ ] Scaling Factor: ________ N/ADC
- [ ] Offset: ________ ADC counts
- [ ] Accuracy: ________ % error

### Notes:
_________________________________________________
_________________________________________________
_________________________________________________

---

**End of Bring-Up, Test, and Calibration Plan**

