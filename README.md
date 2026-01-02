# ESP32-S3 Loadcell Datalogger

A high-performance data acquisition system for precision load cell measurements with synchronized IMU data, designed for industrial testing and research applications.

## Features

- **High-Rate ADC Sampling**: 64,000 samples/second from MAX11270 24-bit delta-sigma ADC
- **6-Axis IMU**: LSM6DSV accelerometer + gyroscope at 120 Hz with FIFO buffering
- **RTC-Disciplined Timestamps**: Sub-microsecond accuracy using RX8900CE 1Hz output
- **Binary + CSV Logging**: Efficient binary format with on-device CSV conversion
- **Multi-Loadcell Calibration**: Store up to 8 calibration curves in NVS
- **Web-Based Administration**: Configure, calibrate, and monitor via WiFi AP
- **Crash Recovery**: Checkpoint-based recovery from power failures
- **Battery Monitoring**: MAX17048 fuel gauge with low-battery protection

## Hardware Requirements

| Component | Part Number | Interface | Function |
|-----------|-------------|-----------|----------|
| MCU | ESP32-S3-MINI-1 | - | Main processor (240 MHz, 320 KB RAM) |
| ADC | MAX11270 | SPI @ 4 MHz | 24-bit load cell digitizer |
| IMU | LSM6DSV16X | I2C @ 400 kHz | 6-axis motion sensor |
| RTC | RX8900CE | I2C @ 400 kHz | Real-time clock with TCXO |
| Fuel Gauge | MAX17048 | I2C @ 400 kHz | LiPo battery monitor |
| Storage | microSD | SDMMC 4-bit | Data logging (Class 10 recommended) |
| LED | WS2812B | GPIO 21 | Status indication |

## Pin Configuration

```
GPIO 2  - Log Button (active HIGH)
GPIO 4  - SD_CLK
GPIO 5  - SD_CMD
GPIO 6-9 - SD_D0-D3
GPIO 10 - SD Card Detect (active LOW)
GPIO 12 - ADC MISO
GPIO 13 - ADC MOSI
GPIO 14 - ADC SYNC
GPIO 15 - ADC RSTB
GPIO 16 - ADC RDYB (data ready)
GPIO 17 - ADC CS
GPIO 18 - ADC SCK
GPIO 21 - NeoPixel Data
GPIO 33 - RTC 1Hz FOUT
GPIO 34 - RTC INT
GPIO 39 - IMU INT1
GPIO 40 - IMU INT2
GPIO 41 - I2C SDA
GPIO 42 - I2C SCL
```

## Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-C cable for programming
- microSD card (FAT32 formatted, Class 10 or better)

### Build and Upload

```bash
# Clone or download the project
cd Loadcell_Datalogger_V1

# Build firmware
pio run -e esp32s3

# Upload firmware
pio run -e esp32s3 -t upload

# Upload Web UI files to SPIFFS
pio run -e esp32s3 -t uploadfs

# Monitor serial output (115200 baud)
pio device monitor
```

### First Boot

1. Insert a microSD card
2. Power on the device
3. Connect to WiFi: `LoadcellLogger-XXXX` (open network)
4. Navigate to `http://192.168.4.1`
5. Configure your load cell calibration in Admin mode

## Operating Modes

### User Mode (Default)
- View live sensor data on dashboard
- Start/stop logging sessions
- Download log files
- LED: Cyan pulse (idle), Green pulse (logging)

### Field Admin Mode
- All User mode features
- Load cell calibration management
- Sensor configuration (gain, sample rates)
- Password: `admin` (configurable)
- LED: Blue pulse

### Factory Mode
- Hardware diagnostics and testing
- Individual sensor tests (ADC, IMU, RTC, SD, Battery)
- LED pattern cycling
- Password: `factory` (configurable)
- LED: Magenta pulse

## LED Status Indicators

| Color | Pattern | Meaning |
|-------|---------|---------|
| Cyan | Pulse | Idle - User mode |
| Blue | Pulse | Idle - Admin mode |
| Magenta | Pulse | Idle - Factory mode |
| Green | Pulse | Logging active |
| Green | Steady | SD card safe to remove |
| Yellow | Fast blink | Preparing/stopping |
| Orange | Pulse | Converting to CSV |
| Red | Blink | Error condition |
| Red | Steady | Critical error |

## Web API Endpoints

### Status & Control
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/status` | System status (mode, WiFi, SD, etc.) |
| GET | `/api/mode` | Current operating mode |
| POST | `/api/mode` | Switch mode (requires password) |
| GET | `/api/live` | Live sensor readings |
| GET | `/api/stream` | SSE stream of sensor data |
| GET | `/api/battery` | Battery status |

### Logging
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/logging/start` | Start logging session |
| POST | `/api/logging/stop` | Stop logging session |
| GET | `/api/session/summary` | Peak values from last session |

### File Management
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/files` | List log files |
| GET | `/api/download?file=X` | Download file |
| POST | `/api/convert?file=X` | Convert .bin to .csv |
| GET | `/api/validate?file=X` | Validate binary file |

### Factory Tests
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/test/adc` | Test ADC |
| POST | `/api/test/imu` | Test IMU |
| POST | `/api/test/rtc` | Test RTC |
| POST | `/api/test/sd` | Test SD card |
| POST | `/api/test/battery` | Test fuel gauge |

## Binary Log Format

Log files use an efficient binary format for high-rate data:

```
[File Header - 64 bytes]
[ADC Record - 12 bytes] × N
[IMU Record - 16 bytes] × M (interleaved)
[Event Records - variable]
[End Record - 9 bytes]
[File Footer - 32 bytes]
```

See [docs/BINARY_FORMAT.md](docs/BINARY_FORMAT.md) for complete specification.

## Python Tools

Parse and analyze log files using the included Python library:

```python
from tools.loadcell_parser import LogFile

# Load binary file
log = LogFile('data/log_1234567890.bin')

# Access data
print(f"Duration: {log.duration_seconds:.2f}s")
print(f"ADC samples: {log.adc_count}")
print(f"IMU samples: {log.imu_count}")

# Iterate samples
for adc in log.iter_adc():
    print(f"{adc.timestamp_s:.6f}: {adc.raw_adc}")
```

Install dependencies:
```bash
pip install -r tools/requirements.txt
```

## Performance Specifications

| Parameter | Value |
|-----------|-------|
| ADC Sample Rate | 64,000 Hz |
| ADC Resolution | 24 bits |
| ADC PGA Gain | 1x to 128x (configurable) |
| IMU Sample Rate | 120 Hz (FIFO batched) |
| IMU Accelerometer | ±2g / ±4g / ±8g / ±16g |
| IMU Gyroscope | ±125 / ±250 / ±500 / ±1000 / ±2000 dps |
| Timestamp Resolution | 1 µs |
| Timestamp Accuracy | ±50 ppm (RTC disciplined) |
| Data Rate to SD | ~800 KB/s |
| Max Recording Duration | Limited by SD card capacity |

## Troubleshooting

### No WiFi network visible
- Check that device has power (LED should be active)
- Wait 5-10 seconds after boot for AP to start

### SD card not detected
- Ensure card is FAT32 formatted
- Check card detect pin (GPIO 10)
- Try a different SD card (some high-capacity cards have compatibility issues)

### ADC reading issues
- Verify SPI connections (MISO, MOSI, SCK, CS)
- Check RDYB signal is connected to GPIO 16
- Ensure load cell excitation voltage is correct

### IMU reads all zeros
- I2C address may differ (0x6A vs 0x6B based on SA0 pin)
- Check WHO_AM_I register response (should be 0x70)

### Build errors
- Run `pio pkg update` to update libraries
- Ensure ESP32 platform is version 6.x or later

## License

This project is provided as-is for educational and research purposes.

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2026-01-01 | Initial release |




