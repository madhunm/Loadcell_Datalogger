# Loadcell Datalogger Binary File Format Specification

**Version:** 1  
**Magic Number:** `0x474C434C` ("LCLG" - LoadCell LoG)  
**Byte Order:** Little-endian

## Overview

The binary log format is designed for high-rate data acquisition (up to 64 ksps) with minimal overhead. Files contain a fixed-size header, variable-length data records, and an optional footer for integrity verification.

```
┌─────────────────────────────────────┐
│          File Header (64 bytes)     │
├─────────────────────────────────────┤
│          ADC Record (12 bytes)      │
├─────────────────────────────────────┤
│          IMU Record (16 bytes)      │
├─────────────────────────────────────┤
│          ADC Record (12 bytes)      │
├─────────────────────────────────────┤
│          Event Record (8+ bytes)    │
├─────────────────────────────────────┤
│              ...                    │
├─────────────────────────────────────┤
│          End Record (9 bytes)       │
├─────────────────────────────────────┤
│          File Footer (32 bytes)     │
└─────────────────────────────────────┘
```

## File Header (64 bytes)

| Offset | Size | Type     | Field              | Description |
|--------|------|----------|--------------------|-----------------------|
| 0      | 4    | uint32   | magic              | `0x474C434C` ("LCLG") |
| 4      | 2    | uint16   | version            | Format version (currently 1) |
| 6      | 2    | uint16   | headerSize         | Size of header (64) |
| 8      | 4    | uint32   | adcSampleRateHz    | ADC sample rate in Hz |
| 12     | 4    | uint32   | imuSampleRateHz    | IMU sample rate in Hz |
| 16     | 8    | uint64   | startTimestampUs   | Unix epoch start time (microseconds) |
| 24     | 32   | char[32] | loadcellId         | Null-terminated loadcell ID |
| 56     | 1    | uint8    | flags              | Bit flags (reserved) |
| 57     | 1    | uint8    | adcGain            | ADC PGA gain setting |
| 58     | 1    | uint8    | adcBits            | ADC resolution (e.g., 24) |
| 59     | 1    | uint8    | imuAccelScale      | IMU accel scale: 0=±2g, 1=±4g, 2=±8g, 3=±16g |
| 60     | 1    | uint8    | imuGyroScale       | IMU gyro scale: 0=±125°/s, 1=±250°/s, etc. |
| 61     | 3    | uint8[3] | reserved           | Reserved (zero-filled) |

### Validation
- `magic` must equal `0x474C434C`
- `version` should be 1
- `headerSize` should be 64

## Data Records

Records are written sequentially after the header. Each record type is identified by its structure.

### ADC Record (12 bytes)

High-rate loadcell samples from the MAX11270 24-bit ADC.

| Offset | Size | Type   | Field              | Description |
|--------|------|--------|--------------------|-----------------------|
| 0      | 4    | uint32 | timestampOffsetUs  | Microseconds since file start |
| 4      | 4    | int32  | rawAdc             | Raw 24-bit ADC value (sign-extended) |
| 8      | 4    | uint32 | sequenceNum        | Monotonic counter for gap detection |

**Converting raw ADC to physical units:**
```python
# ADC to microvolts (assuming Vref=2.5V, 24-bit, gain=1)
uV = rawAdc * (2500000.0 / 8388608.0) / gain

# Microvolts to kg (requires calibration curve)
kg = calibration.interpolate(uV)

# Kg to Newtons
N = kg * 9.81
```

### IMU Record (16 bytes)

6-axis motion data from the LSM6DSV16X IMU.

| Offset | Size | Type   | Field              | Description |
|--------|------|--------|--------------------|-----------------------|
| 0      | 4    | uint32 | timestampOffsetUs  | Microseconds since file start |
| 4      | 2    | int16  | accelX             | Accelerometer X (raw LSB) |
| 6      | 2    | int16  | accelY             | Accelerometer Y (raw LSB) |
| 8      | 2    | int16  | accelZ             | Accelerometer Z (raw LSB) |
| 10     | 2    | int16  | gyroX              | Gyroscope X (raw LSB) |
| 12     | 2    | int16  | gyroY              | Gyroscope Y (raw LSB) |
| 14     | 2    | int16  | gyroZ              | Gyroscope Z (raw LSB) |

**Converting raw IMU to physical units:**
```python
# LSM6DSV16X at ±2g scale: 0.061 mg/LSB
ACCEL_SCALE = 0.061 / 1000.0  # mg/LSB to g/LSB
accel_g = raw_accel * ACCEL_SCALE

# Gyroscope at ±125°/s scale: 4.375 mdps/LSB
GYRO_SCALE = 4.375 / 1000.0  # mdps/LSB to dps/LSB
gyro_dps = raw_gyro * GYRO_SCALE
```

### Event Record (8+ bytes)

Marks significant events during logging.

| Offset | Size | Type   | Field              | Description |
|--------|------|--------|--------------------|-----------------------|
| 0      | 4    | uint32 | timestampOffsetUs  | Microseconds since file start |
| 4      | 2    | uint16 | eventCode          | Event type code |
| 6      | 2    | uint16 | dataLength         | Length of optional event data |
| 8      | N    | uint8[]| data               | Optional event-specific data |

### Event Codes

| Code   | Name             | Description |
|--------|------------------|-------------|
| 0x0001 | SessionStart     | Logging session started |
| 0x0002 | SessionEnd       | Logging session ended |
| 0x0010 | ButtonPress      | User pressed button |
| 0x0020 | Overflow         | Buffer overflow occurred |
| 0x0030 | SyncLost         | RTC synchronization lost |
| 0x0031 | SyncRestored     | RTC synchronization restored |
| 0x0100 | CalibrationPoint | Calibration reference point |
| 0x00F0 | Checkpoint       | Periodic checkpoint marker |
| 0x00F1 | FileRotation     | File rotation occurred |
| 0x00F2 | LowBattery       | Low battery warning |
| 0x00F3 | Saturation       | ADC saturation detected |
| 0x00F4 | WriteLatency     | High SD write latency |
| 0x00F5 | Recovery         | Session recovered from crash |

## End Record (9 bytes)

Marks the end of the data stream.

| Offset | Size | Type   | Field        | Description |
|--------|------|--------|--------------|-------------|
| 0      | 1    | uint8  | type         | `0xFF` |
| 1      | 4    | uint32 | totalRecords | Total number of records |
| 5      | 4    | uint32 | checksum     | CRC32 of all data |

## File Footer (32 bytes)

Written on clean shutdown for integrity verification. Absence indicates unclean shutdown.

| Offset | Size | Type   | Field            | Description |
|--------|------|--------|------------------|-------------|
| 0      | 4    | uint32 | magic            | `0xF007F007` |
| 4      | 8    | uint64 | totalAdcSamples  | Total ADC samples written |
| 12     | 8    | uint64 | totalImuSamples  | Total IMU samples written |
| 20     | 4    | uint32 | droppedSamples   | Samples lost to overflow |
| 24     | 4    | uint32 | endTimestampUs   | Final timestamp offset |
| 28     | 4    | uint32 | crc32            | CRC32 of all file data |

### Validation
- `magic` must equal `0xF007F007`
- If footer is missing, session ended uncleanly (power loss, crash)

## Parsing Algorithm (Pseudocode)

```python
def parse_log_file(filepath):
    with open(filepath, 'rb') as f:
        # Read and validate header
        header = read_header(f)
        if header.magic != 0x474C434C:
            raise ValueError("Invalid file magic")
        
        # Read records until end or EOF
        while True:
            # Peek at position to determine record type
            pos = f.tell()
            
            # Try to read as ADC record
            data = f.read(12)
            if len(data) < 12:
                break
            
            # Check if this might be end record (type = 0xFF at start)
            if data[0] == 0xFF and pos > 64:
                # Rewind and read as end record
                f.seek(pos)
                end_record = read_end_record(f)
                break
            
            # Parse as ADC record
            adc = parse_adc_record(data)
            yield adc
            
            # Interleave IMU records based on decimation ratio
            # ...
        
        # Try to read footer
        footer = read_footer(f)
        if footer and footer.magic == 0xF007F007:
            validate_crc32(filepath, footer.crc32)
```

## File Size Estimation

```python
# Bytes per second
ADC_RECORD_SIZE = 12
IMU_RECORD_SIZE = 16
data_rate = (adc_rate_hz * ADC_RECORD_SIZE) + (imu_rate_hz * IMU_RECORD_SIZE)

# File size for 1 hour at 64kHz ADC, 1kHz IMU
# = 64000 * 12 + 1000 * 16 = 768,000 + 16,000 = 784,000 bytes/sec
# = ~2.8 GB/hour
```

## CRC32 Calculation

The CRC32 is computed incrementally during writing using the ESP32's hardware CRC32 with polynomial `0x04C11DB7` (IEEE 802.3). The CRC covers all data from the header through the end record (excluding the footer).

```c
#include <esp_crc.h>
uint32_t crc = esp_crc32_le(0, data, length);
```

## Gap Detection

The `sequenceNum` field in ADC records is a monotonically increasing counter. Gaps in sequence numbers indicate dropped samples:

```python
expected_seq = 0
for record in adc_records:
    if record.sequenceNum != expected_seq:
        gap = record.sequenceNum - expected_seq
        print(f"Gap detected: {gap} samples missing")
    expected_seq = record.sequenceNum + 1
```

## Version History

| Version | Date       | Changes |
|---------|------------|---------|
| 1       | 2026-01-01 | Initial format |

