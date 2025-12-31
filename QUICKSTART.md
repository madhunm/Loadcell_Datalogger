# Quick Start Guide

## Build & Flash (5 minutes)

### Prerequisites
- PlatformIO installed (VS Code extension or CLI)
- ESP32-S3 board connected via USB

### Steps

1. **Extract and open project:**
   ```bash
   unzip loadcell_logger.zip
   cd loadcell_logger
   ```

2. **Build:**
   ```bash
   pio run
   ```

3. **Upload:**
   ```bash
   pio run -t upload
   ```

4. **Monitor:**
   ```bash
   pio device monitor
   ```

## First Use

1. **System boots** → NeoPixel shows **cyan pulse** (Admin Mode)

2. **Connect to WiFi:**
   - SSID: `LoadcellLogger-XXXX` (no password)
   - Open browser: `http://192.168.4.1`

3. **Add calibration** (using curl):
   ```bash
   curl -X POST http://192.168.4.1/api/loadcells \
     -H "Content-Type: application/json" \
     -d '{
       "id": "MyLoadCell-001",
       "model": "S-Beam",
       "serial": "001",
       "capacity_kg": 1000,
       "excitation_V": 10.0,
       "points": [
         {"load_kg": 0, "output_uV": 0},
         {"load_kg": 500, "output_uV": 1000},
         {"load_kg": 1000, "output_uV": 2000}
       ]
     }'
   ```

4. **Set active:**
   ```bash
   curl -X PUT http://192.168.4.1/api/active \
     -H "Content-Type: application/json" \
     -d '{"id": "MyLoadCell-001"}'
   ```

5. **Start logging:**
   - Press the button → LED turns **green** (Logging Mode)
   - Data acquisition active at 64,000 samples/sec

6. **Stop logging:**
   - Press button again
   - LED turns **yellow** → **orange** → **green steady**
   - CSV file ready on SD card

## LED States

| Color | Pattern | Meaning |
|-------|---------|---------|
| Cyan | Pulse | Admin mode (WiFi active) |
| Yellow | Fast blink | Preparing to log |
| Green | Pulse | Logging active |
| Yellow | Pulse | Stopping, flushing |
| Orange | Pulse | Converting to CSV |
| Green | Steady | Ready, SD safe to remove |
| Red | Fast blink | Error |

## File Output

After logging, check your SD card for:
- `/data/log_YYYYMMDD_HHMMSS.bin` - Raw binary data
- `/data/log_YYYYMMDD_HHMMSS.csv` - Converted CSV

Enjoy your high-performance data logger!
