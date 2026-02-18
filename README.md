# Parachute Data Logger (Template)

Restart-friendly scaffold for a parachute data logger:

- MAX11270 load cell ADC @ 64 ksps acquisition
- 500 Hz frame logging to SD (binary `.BIN`)
- Serial CLI + Windows host tool for long captures + matplotlib plotting
- No Wi-Fi / WebUI

**Branch `restartCoreLogger`:** Full pin map, button/LED behavior, log format, and build/usage are in **[docs/restartCoreLogger/README.md](docs/restartCoreLogger/README.md)**. On this branch, IMU (LSM6DSV), RTC (RX8900CE), and fuel gauge (MAX17048) are implemented; pin source of truth is `src/pins.h`.

## Hardware pins (as configured)
Pin mapping is defined in **`src/pins.h`**. ADC (MAX11270): MISO 12, MOSI 13, SYNC 14, RSTB 15, RDYB 16, CS 17, SCK 18. ADC CLK tied to GND → internal clock (EXTCK=0). SD_MMC 4-bit: CLK 4, CMD 5, D0–D3 6–9. I2C 41/42; IMU INT 39/40; RTC 33/34.

## Serial CLI
115200 baud. Commands:
- `help`
- `status`
- `scope <hz>` (hz must divide 500; 10/20/25/50 recommended)
- `scope 0`
- `startlog` / `stoplog`

Scope fields:
`ms,force_mean_N,force_peak_N,accel_mag_g,flags`

## Windows host plotting tool
```bat
pip install pyserial matplotlib numpy
python tools\host\scope_plot.py
```

## BIN -> CSV for Excel
```bat
python tools\host\bin2csv.py E:\LOG0000.BIN E:\LOG0000.csv
```

## Notes
- SD logging uses SD_MMC 4-bit. If your hardware uses SPI SD, adjust `src/services/sd_logger.cpp`.
- See `docs/restartCoreLogger/README.md` for restartCoreLogger branch details.
