# Loadcell Datalogger

**High-speed data acquisition system for load cell and IMU measurements**

[![ESP32-S3](https://img.shields.io/badge/ESP32--S3-DevKitC1-blue)](https://www.espressif.com/en/products/devkits/esp32-s3-devkitc-1/overview)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Enabled-green)](https://platformio.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

## Overview

The Loadcell Datalogger is an embedded firmware system for the ESP32-S3 that captures high-speed force and acceleration data from load cells during dynamic events. The system features:

- **64 ksps ADC sampling** (24-bit resolution)
- **960 Hz IMU data** (6-axis accelerometer and gyroscope)
- **Real-time clock synchronization** for timestamped data
- **Dual-core architecture** for optimal performance
- **Web-based configuration portal** for remote setup
- **Visual status indicators** via NeoPixel LED
- **Data integrity verification** with CRC32 checksums

## Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- ESP32-S3 DevKitC1 board
- SD card (Class 10, FAT32 formatted)
- Required hardware modules (see [Hardware Setup](#hardware-setup))

### Building and Flashing

```bash
# Clone the repository
git clone https://github.com/madhunm/Loadcell_Datalogger.git
cd Loadcell_Datalogger

# Build the project
pio run

# Upload to ESP32-S3
pio run -t upload

# Monitor serial output
pio device monitor
```

### First Boot

1. Power on the ESP32-S3
2. Watch the NeoPixel LED:
   - **Blue (solid)**: Initializing
   - **Green (breathing)**: Ready to log
3. Connect to WiFi: `Loadcell_Datalogger_XXXX` (no password)
4. Open browser: `http://192.168.4.1`

## Documentation

- **[User Manual](USER_MANUAL.md)** - Complete user guide (A4 printable format)
- **[Bring-Up & Calibration Plan](BRINGUP_TEST_CALIBRATION_PLAN.md)** - Step-by-step setup and calibration
- **[Code Documentation Summary](CODE_DOCUMENTATION_SUMMARY.md)** - Code structure and conventions
- **[UX Enhancements](UX_ENHANCEMENTS_RECOMMENDATIONS.md)** - UI/UX improvement recommendations

## Hardware Setup

### Required Components

- ESP32-S3 DevKitC1
- MAX11270 24-bit ADC module
- LSM6DSV16X IMU module
- RX8900CE RTC module
- SD card socket with card detect
- NeoPixel LED (WS2812B)
- LOG_START button
- Load cell with signal conditioning

### Pin Assignments

See [`include/pins.h`](include/pins.h) for complete pin definitions.

**Key Connections:**
- **SD Card**: GPIO 4-10 (SDMMC 4-bit)
- **ADC (SPI)**: GPIO 12-18
- **IMU/RTC (I2C)**: GPIO 41-42
- **NeoPixel**: GPIO 21
- **Button**: GPIO 2

## Features

### Data Acquisition
- **ADC**: Up to 64,000 samples/second, 24-bit resolution
- **IMU**: Up to 960 Hz, ±16g accelerometer, ±2000 dps gyroscope
- **Synchronization**: RTC-based timestamps with sub-millisecond accuracy

### Data Storage
- **Format**: Binary files for speed, CSV for analysis
- **Location**: `/log/` directory on SD card
- **Naming**: `YYYYMMDD_HHMMSS_ADC.bin`, `YYYYMMDD_HHMMSS_IMU.bin`, `YYYYMMDD_HHMMSS.csv`
- **Integrity**: CRC32 checksums for verification

### Web Interface
- **Status Dashboard**: Real-time system status and metrics
- **Data Visualization**: Interactive charts with dual Y-axes
- **File Comparison**: Overlay multiple CSV files
- **Calibration Portal**: Loadcell calibration and advanced settings
- **Auto-refresh**: Configurable automatic updates

### Status Indicators
- **NeoPixel LED**: Visual feedback following ANSI Z535.1 / IEC 60073 standards
- **Patterns**: Initialization, ready, logging, converting, errors
- **Web Portal**: Real-time status indicators and health metrics

## System Architecture

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
└─────────────────────────────────────────┘
```

## Configuration

### Default Settings

- **ADC**: 64 ksps, PGA gain x4
- **IMU**: 960 Hz, ±16g accel, ±2000 dps gyro
- **WiFi**: Access point mode, no password

### Web Configuration

Access the web portal at `http://192.168.4.1` to configure:
- ADC sample rate and PGA gain
- IMU sample rate and ranges
- Loadcell calibration (scaling factor and offset)

### Calibration Portal

Hidden portal at `http://192.168.4.1/cal` for:
- Loadcell calibration constants
- Advanced ADC/IMU settings
- Configuration persistence

## Usage

### Starting a Logging Session

1. Ensure system is in READY state (breathing green LED)
2. Press LOG_START button
3. System begins logging (solid green LED)
4. Press button again to stop

### Data Conversion

After stopping:
1. System automatically converts binary files to CSV
2. NeoPixel shows yellow blinking (DO NOT remove SD card)
3. When complete, shows green double-blink (safe to remove)

### Viewing Data

1. Remove SD card (after safe-to-remove pattern)
2. Insert into computer
3. Open CSV files in spreadsheet software
4. Or use web portal to view latest data

## Troubleshooting

### Common Issues

- **SD Card Not Detected**: Check card format (FAT32), connections, card detect pin
- **ADC Initialization Failed**: Verify SPI connections, check RDYB pin
- **IMU Initialization Failed**: Check I2C connections and pull-ups
- **Write Failures**: Use Class 10+ SD card, check free space

See [User Manual - Troubleshooting](USER_MANUAL.md#9-troubleshooting) for detailed solutions.

## Project Structure

```
Loadcell_Datalogger/
├── include/           # Header files
│   ├── pins.h        # GPIO pin definitions
│   ├── logger.h      # Logger module interface
│   ├── adc.h         # ADC driver interface
│   ├── imu.h         # IMU driver interface
│   └── ...
├── src/              # Source files
│   ├── main.cpp      # Main application
│   ├── logger.cpp    # Logger implementation
│   ├── adc.cpp       # ADC driver
│   └── ...
├── platformio.ini    # PlatformIO configuration
├── USER_MANUAL.md    # User documentation
├── BRINGUP_TEST_CALIBRATION_PLAN.md
└── README.md         # This file
```

## Development

### Building

```bash
# Build for release (disable debug)
# Edit platformio.ini: set LOGGER_DEBUG=0

# Build with debug output
pio run -e esp32-s3-devkitc1-n4r2
```

### Debug Flags

Configure in `platformio.ini`:
- `LOGGER_DEBUG=1` - Logger debug output
- `ADC_DEBUG=0` - ADC debug output
- `IMU_DEBUG=0` - IMU debug output
- `WEBCONFIG_DEBUG=1` - Web config debug output

### Code Style

- Variables: `camelCase`
- Static variables: `s_` prefix with `camelCase`
- Constants: `UPPER_CASE`
- Functions: `camelCase`
- See [Code Documentation Summary](CODE_DOCUMENTATION_SUMMARY.md) for details

## Specifications

| Parameter | Value |
|-----------|-------|
| **ADC Sample Rate** | 1,000 - 64,000 Hz |
| **ADC Resolution** | 24-bit signed |
| **IMU Sample Rate** | 15 - 960 Hz |
| **IMU Accelerometer** | ±2g, ±4g, ±8g, ±16g |
| **IMU Gyroscope** | ±125 to ±2000 dps |
| **Data Rate** | ~1-2 MB/minute |
| **Power** | 5V, ~200-300 mA typical |

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## Support

For issues, questions, or contributions:
- Open an issue on GitHub
- Review the [User Manual](USER_MANUAL.md)
- Check the [Troubleshooting Guide](USER_MANUAL.md#9-troubleshooting)

## Acknowledgments

- ESP32-S3 platform by Espressif
- PlatformIO for build system
- Adafruit NeoPixel library
- SparkFun LSM6DSV16X library
- RX8900 RTC library

---

**Version:** 1.0  
**Last Updated:** December 2024

