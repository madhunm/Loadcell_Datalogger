# ESP32-S3 Load Cell Data Logger

High-rate load cell + IMU data acquisition system with RTC-disciplined timestamps, button-controlled logging sessions, and web-based calibration management.

## Features

- **High-rate acquisition**: 64,000 samples/sec from 24-bit ADC (MAX11270)
- **Synchronized IMU data**: 1,000 samples/sec (LSM6DSV 6-axis)
- **Precision timestamps**: RTC-disciplined (RX8900CE) with 1Hz sync
- **Large capacity**: SDMMC 4-bit interface for fast SD card writes
- **Per-loadcell calibration**: Store multiple calibration curves, switch between loadcells
- **Admin WebUI**: WiFi AP mode for easy calibration management
- **Button control**: Simple start/stop operation
- **Status LED**: NeoPixel indicates system state
- **On-device CSV export**: Binary to CSV conversion after logging

## Hardware Requirements

- ESP32-S3 development board (ESP32-S3-DevKitC-1 or compatible)
- MAX11270 24-bit ADC
- LSM6DSV 6-axis IMU
- RX8900CE Real-Time Clock
- MicroSD card (Class 10 or better recommended)
- NeoPixel RGB LED
- Momentary push button
- Load cell with appropriate signal conditioning

## Pin Configuration

See `src/pin_config.h` for complete GPIO mapping.

| Function | GPIO | Notes |
|----------|------|-------|
| Button | 2 | Active HIGH |
| NeoPixel | 21 | Status LED |
| SD Card | 4-10 | SDMMC 4-bit |
| MAX11270 SPI | 12-18 | Load cell ADC |
| I2C (RTC+IMU) | 41-42 | Shared bus |
| RTC 1Hz Sync | 33 | Timestamp discipline |

## Quick Start

### 1. Hardware Setup

1. Connect all sensors according to pin configuration
2. Insert formatted SD card
3. Connect USB for power and serial monitoring

### 2. Build and Upload

```bash
# Using PlatformIO
pio run -t upload
pio device monitor
```

### 3. Initial Configuration

1. System boots into **Admin Mode** (cyan pulsing LED)
2. Connect to WiFi AP: `LoadcellLogger-XXXX` (no password)
3. Navigate to http://192.168.4.1
4. Add your first loadcell calibration via API

### 4. Adding Calibration Data

Use HTTP client (curl, Postman, or custom script):

```bash
# Create new loadcell
curl -X POST http://192.168.4.1/api/loadcells \
  -H "Content-Type: application/json" \
  -d '{
    "id": "TC023L0-000025",
    "model": "TC023L0",
    "serial": "000025",
    "capacity_kg": 2000,
    "excitation_V": 10.0,
    "sensitivity_mVV": 2.0,
    "points": [
      {"load_kg": 0, "output_uV": 0},
      {"load_kg": 500, "output_uV": 1000},
      {"load_kg": 1000, "output_uV": 2000},
      {"load_kg": 2000, "output_uV": 4000}
    ]
  }'

# Set as active
curl -X PUT http://192.168.4.1/api/active \
  -H "Content-Type: application/json" \
  -d '{"id": "TC023L0-000025"}'
```

### 5. Data Acquisition

1. **Press button** → Transition to **Logging Mode** (green pulsing)
   - WiFi shuts down automatically
   - Data acquisition starts
   - Binary log file created on SD card

2. **Press button again** → Stop logging
   - Buffers flushed (yellow pulse)
   - Binary to CSV conversion (orange pulse)
   - SD safe to remove (green steady)

3. **Wait 30 seconds or press button** → Return to Admin Mode

## System States

| State | LED | WiFi | Description |
|-------|-----|------|-------------|
| **Admin** | Cyan pulse | ON | WebUI active for configuration |
| **PreLog** | Yellow fast | Turning OFF | Shutting down WiFi stack |
| **Logging** | Green pulse | OFF | Active data acquisition |
| **Stopping** | Yellow pulse | OFF | Flushing buffers |
| **Converting** | Orange pulse | OFF | Binary → CSV conversion |
| **Ready** | Green steady | OFF | SD safe to remove |

## Log File Format

### Binary Format (`.bin`)

- **Header** (64 bytes): Metadata, timestamps, loadcell ID
- **Loadcell samples** (8 bytes each): Timestamp + raw ADC value
- **IMU samples** (16 bytes each): Timestamp + 6-axis data

### CSV Format (`.csv`)

Auto-generated after logging:
```csv
timestamp_us,timestamp_iso,sample_type,raw_adc,load_kg,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z
0,2025-01-01T00:00:00.000000Z,LOADCELL,123456,45.678,,,,,,
15,2025-01-01T00:00:00.000015Z,LOADCELL,123470,45.682,,,,,,
1000,2025-01-01T00:00:00.001000Z,IMU,,,1234,5678,-9012,234,567,-890
```

## API Reference

### GET /api/loadcells
List all stored loadcell calibrations

### POST /api/loadcells
Create new loadcell calibration (JSON body)

### PUT /api/active
Set active loadcell: `{"id": "..."}` (JSON body)

### GET /api/active
Get currently active loadcell ID

### GET /api/status
System status and live ADC reading

## Project Structure

```
src/
├── main.cpp                      # Entry point
├── pin_config.h                  # GPIO definitions
├── app/
│   └── state_machine.h/.cpp      # Main coordinator
├── calibration/
│   ├── loadcell_types.h          # Data structures
│   ├── calibration_storage.h/.cpp# NVS persistence
│   └── calibration_interp.h/.cpp # µV → kg conversion
├── drivers/
│   ├── max11270_driver.h/.cpp    # 24-bit ADC
│   ├── lsm6dsv_driver.h/.cpp     # 6-axis IMU
│   ├── rx8900ce_driver.h/.cpp    # RTC with 1Hz sync
│   ├── sd_manager.h/.cpp         # SD card operations
│   └── status_led.h/.cpp         # NeoPixel control
├── logging/
│   ├── binary_format.h           # Log file structures
│   ├── timestamp_sync.h/.cpp     # RTC discipline
│   ├── logger_module.h/.cpp      # Core acquisition
│   └── bin_to_csv.h/.cpp         # Post-processing
└── network/
    ├── wifi_ap.h/.cpp            # Access point
    └── admin_webui.h/.cpp        # HTTP REST API
```

## Performance Characteristics

- **ADC sample rate**: 64,000 Hz (configurable)
- **IMU sample rate**: 1,000 Hz (decimated from ADC)
- **Timestamp accuracy**: <1 ms (RTC-disciplined)
- **Ring buffer**: 32 KB
- **Write buffers**: 2× 8 KB (double-buffered)
- **Throughput**: ~512 KB/s sustained to SD

## Dual-Core Architecture

- **Core 0**: SD writes, buffer drain, state machine, WiFi (when enabled)
- **Core 1**: ADC ISR, IMU sync reads, ring buffer fill

WiFi is completely disabled during logging to maximize Core 0 availability for SD operations.

## Calibration Curve Format

Each loadcell stores up to 16 calibration points:
- Points must be sorted by increasing `output_uV`
- Linear interpolation between points
- Extrapolation beyond range uses nearest two points
- Result clamped to [0, capacity_kg]

## Troubleshooting

### SD Card Not Detected
- Check card detect pin (GPIO 10)
- Verify card is formatted (FAT32 recommended)
- Check SDMMC pin connections

### ADC Not Responding
- Verify SPI connections (MISO, MOSI, SCK, CS)
- Check RSTB and RDYB pins
- Ensure 2.5V reference voltage present

### WiFi Not Starting
- ESP32-S3 may need antenna connected
- Check for conflicts with other 2.4GHz devices
- Try power cycling

### No RTC Sync
- Verify I2C connections (SDA, SCL)
- Check 1Hz FOUT signal on GPIO 33
- Ensure RTC battery is installed

## License

MIT License - Feel free to use and modify.

## Authors

ESP32-S3 Load Cell Data Logger Project
