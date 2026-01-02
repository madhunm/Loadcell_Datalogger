# Loadcell Datalogger Test Suite

This directory contains the complete test framework for the Loadcell Datalogger project, implementing a 3-tier testing strategy.

## Test Structure

```
test/
├── mocks/                      # Arduino mocks for native testing
│   ├── Arduino.h               # Main Arduino mock header
│   ├── mock_arduino.h          # Type definitions and function mocks
│   └── mock_arduino.cpp        # Mock implementations
├── native/                     # Tier 1: Unit tests (runs on PC)
│   ├── test_ring_buffer.cpp    # SPSC ring buffer tests
│   ├── test_binary_format.cpp  # Binary log format tests
│   └── test_calibration.cpp    # Calibration interpolation tests
├── embedded/                   # Tier 2: Integration tests (runs on ESP32)
│   ├── test_adc_integration.cpp    # MAX11270 ADC tests
│   ├── test_imu_integration.cpp    # LSM6DSV IMU tests
│   └── test_sd_integration.cpp     # SD card manager tests
└── README.md                   # This file
```

## Running Tests

### Tier 1: Native Unit Tests (No Hardware Required)

Run on your development PC without any hardware:

```bash
# Run all native tests
pio test -e native

# Run specific test file
pio test -e native --filter "test_ring_buffer"

# Verbose output
pio test -e native -v
```

### Tier 2: Embedded Integration Tests (Requires Hardware)

Run on an ESP32-S3 with the complete hardware setup:

```bash
# Connect ESP32-S3 via USB, then:
pio test -e esp32s3_test

# Run specific test suite
pio test -e esp32s3_test --filter "test_adc"

# Specify upload port if needed
pio test -e esp32s3_test --upload-port COM3
```

### Tier 3: System Tests (Requires Running Device)

Python-based end-to-end tests via HTTP API:

```bash
cd tools/system_tests

# Install dependencies
pip install -r requirements.txt

# Run tests (device must be running in Admin mode)
pytest -v

# Run with custom device IP
pytest --device-ip=192.168.4.1

# Run only fast tests (skip stress tests)
pytest -v -m "not slow"

# Run stress tests
pytest -v -m slow
```

## Test Categories

### Native Unit Tests

| Test File | Tests | Purpose |
|-----------|-------|---------|
| `test_ring_buffer.cpp` | 20+ | SPSC ring buffer: push, pop, overflow, batch operations |
| `test_binary_format.cpp` | 35+ | Binary format: struct sizes, packing, validation |
| `test_calibration.cpp` | 20+ | Calibration: interpolation, extrapolation, edge cases |

### Embedded Integration Tests

| Test File | Tests | Purpose |
|-----------|-------|---------|
| `test_adc_integration.cpp` | 15+ | MAX11270: init, single reads, continuous mode, stats |
| `test_imu_integration.cpp` | 18+ | LSM6DSV: init, accel, gyro, FIFO, ODR settings |
| `test_sd_integration.cpp` | 20+ | SD card: mount, read/write, large files, performance |

### System Tests

| Test File | Tests | Purpose |
|-----------|-------|---------|
| `test_api_status.py` | 15+ | Status, live data, battery, files API endpoints |
| `test_logging_session.py` | 12+ | Complete logging sessions, file validation |
| `test_led_status.py` | 6+ | LED control and state indication |
| `test_stress.py` | 5+ | Long-running stress and recovery tests |

## Writing New Tests

### Native Tests

```cpp
#include <unity.h>
#include "your_module.h"

void setUp() { /* per-test setup */ }
void tearDown() { /* per-test cleanup */ }

void test_feature_works() {
    TEST_ASSERT_TRUE(feature_works());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_feature_works);
    return UNITY_END();
}
```

### Embedded Tests

```cpp
#include <unity.h>
#include <Arduino.h>
#include "your_driver.h"

void test_driver_init() {
    TEST_ASSERT_TRUE(Driver::init());
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    UNITY_BEGIN();
    RUN_TEST(test_driver_init);
    UNITY_END();
}

void loop() { delay(1000); }
```

### Python System Tests

```python
import pytest

class TestYourFeature:
    def test_api_responds(self, device_api):
        response = device_api.get("/api/your-endpoint")
        assert response.status_code == 200
    
    def test_with_logging(self, device_api, ensure_not_logging):
        # ensure_not_logging fixture handles start/stop cleanup
        device_api.start_logging()
        # ... test ...
        device_api.stop_logging()
```

## Test Fixtures

### Python Fixtures (conftest.py)

- `device_api` - DeviceAPI helper with common methods
- `device_ip` - Configured device IP address
- `ensure_not_logging` - Ensures logging is stopped before/after test
- `device_connected` - Skips tests if device unreachable
- `output_dir` - Temporary directory for test artifacts

## CI/CD Integration

For GitHub Actions or similar:

```yaml
name: Tests

on: [push, pull_request]

jobs:
  native-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - uses: actions/cache@v3
        with:
          path: ~/.platformio
          key: ${{ runner.os }}-pio
      - name: Install PlatformIO
        run: pip install platformio
      - name: Run native tests
        run: pio test -e native
```

## Troubleshooting

### Native Tests Won't Compile

1. Check mock headers are in `test/mocks/`
2. Verify `-I test/mocks` in platformio.ini
3. Check for missing mock implementations

### Embedded Tests Fail to Upload

1. Verify correct COM port
2. Check ESP32-S3 is in bootloader mode
3. Try lower upload speed (115200)

### System Tests Can't Connect

1. Verify device is in Admin mode (WiFi AP active)
2. Connect to device WiFi network
3. Check IP address (default: 192.168.4.1)
4. Verify firewall allows HTTP connections

## Coverage Goals

| Tier | Target | Current |
|------|--------|---------|
| Unit | 80% pure logic | ~90% |
| Integration | All drivers | 100% |
| System | Core use cases | ~80% |




