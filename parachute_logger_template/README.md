# Parachute Data Logger (Template)

Restart-friendly scaffold for a parachute data logger:

- MAX11270 load cell ADC @ 64 ksps acquisition
- 500 Hz frame logging to SD (binary `.BIN`)
- Serial CLI + Windows host tool for long captures + matplotlib plotting
- No Wi-Fi / WebUI

## Hardware pins (as configured)
ADC (MAX11270) pins:
- MISO: IO12
- MOSI: IO13
- SYNC: IO14
- RSTB: IO15
- RDYB: IO16
- CS:   IO17
- SCK:  IO18

ADC CLK pin tied to GND -> internal clock (EXTCK=0).

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
- SD logging uses `SD_MMC` by default. If your hardware uses SPI SD, adjust `src/services/sd_logger.cpp`.
- IMU and fuel gauge tasks are stubbed (zeros) in this template; integrate your drivers next.
