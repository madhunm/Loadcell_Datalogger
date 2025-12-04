# Changelog

All notable changes to the Loadcell Datalogger project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2024-12-XX

### Added
- Initial release of Loadcell Datalogger firmware
- High-speed ADC sampling (up to 64 ksps, 24-bit)
- IMU data acquisition (up to 960 Hz, 6-axis)
- Real-time clock synchronization
- Dual-core architecture (Core 0: sampling, Core 1: logging)
- SD card data storage (binary and CSV formats)
- Web-based configuration portal
- WiFi access point with persistent SSID
- NeoPixel status indicators (ANSI Z535.1 / IEC 60073 compliant)
- Data integrity verification (CRC32 checksums)
- Automatic binary-to-CSV conversion
- Watchdog timer support
- Write failure detection and retry logic
- SD card removal detection
- Buffer overflow detection
- Loadcell calibration support
- Data comparison feature (overlay multiple CSV files)
- Auto-refresh functionality in web portal
- Color-coded peak statistics
- Progress bar for SD card storage visualization
- Comprehensive user manual
- Bring-up and calibration plan
- Code documentation

### Features
- **ADC Module**: MAX11270 24-bit SPI ADC driver
  - Configurable sample rates (1-64 ksps)
  - Programmable PGA gain (x1 to x128)
  - Self-calibration support
  - Ring buffer for high-speed sampling

- **IMU Module**: LSM6DSV16X 6-axis IMU driver
  - Configurable output data rates (15-960 Hz)
  - Configurable accelerometer ranges (±2g to ±16g)
  - Configurable gyroscope ranges (±125 to ±2000 dps)
  - Ring buffer for continuous sampling

- **RTC Module**: RX8900CE real-time clock
  - 1 Hz update interrupt
  - Timestamp synchronization
  - Compile-time initialization

- **Logger Module**: Data logging and conversion
  - Binary file format for speed
  - CSV conversion with data alignment
  - Write buffering for efficiency
  - CRC32 data integrity checks
  - FreeRTOS task for non-blocking conversion

- **Web Portal**: Configuration and visualization
  - Status dashboard with real-time metrics
  - Data visualization with Chart.js
  - Dual Y-axes (Force/Acceleration)
  - Moving average filter
  - File comparison feature
  - Calibration portal (hidden)
  - Auto-refresh with configurable interval

- **NeoPixel Status**: Visual feedback system
  - Breathing animation for READY state
  - Blink patterns for errors
  - ANSI/IEC standard color coding
  - Real-time status indication

### Documentation
- User Manual (A4 printable format)
- Bring-Up, Test, and Calibration Plan
- Code Documentation Summary
- Pin assignment documentation
- API documentation in headers

### Technical Details
- **Platform**: ESP32-S3 DevKitC1
- **Framework**: Arduino (via PlatformIO)
- **FreeRTOS**: Multi-core task management
- **Storage**: SDMMC 4-bit interface
- **Communication**: WiFi AP, I2C, SPI

### Known Limitations
- CSV conversion is single-threaded (may take time for long sessions)
- Maximum session duration limited by SD card capacity
- Web portal requires WiFi connection
- Calibration values must be set via web portal (not hardcoded in firmware)

### Security Notes
- WiFi access point has no password (intended for local use only)
- Web portal has no authentication (local network only)
- Calibration portal is hidden but not password-protected

## [Unreleased]

### Planned Features
- Password protection for WiFi access point
- Web portal authentication
- Data export in additional formats (JSON, HDF5)
- Real-time data streaming over WiFi
- Mobile app for remote monitoring
- Advanced filtering and analysis tools
- Multi-session batch processing
- Cloud upload capability
- Enhanced error recovery mechanisms

### Potential Improvements
- Interrupt-driven ADC sampling (currently polling)
- Interrupt-driven IMU sampling (currently polling)
- DMA for SPI transfers
- Compression for binary files
- Incremental CSV conversion (streaming)
- WebSocket for real-time updates
- OTA firmware updates
- Configuration backup/restore

---

## Version History

- **1.0.0** (2024-12-XX): Initial release

---

**Note**: Dates are in YYYY-MM-DD format.

