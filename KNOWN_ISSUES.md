# Known Issues and Limitations

This document tracks known issues, limitations, and workarounds for the Loadcell Datalogger system.

## Current Issues

### None Reported

No critical issues are currently known. If you encounter a problem, please open an issue on GitHub.

## Limitations

### 1. CSV Conversion Performance

**Issue**: CSV conversion is single-threaded and may take significant time for very long logging sessions (e.g., >30 minutes).

**Impact**: 
- 10-minute session: ~10-30 seconds conversion time
- 30-minute session: ~1-3 minutes conversion time
- 60-minute session: ~2-6 minutes conversion time

**Workaround**: 
- Keep logging sessions to reasonable durations (<30 minutes)
- Conversion runs in background task, system remains responsive
- Monitor NeoPixel pattern to know when conversion is complete

**Future Improvement**: Implement incremental/streaming conversion or multi-threaded processing.

### 2. SD Card Write Speed Dependency

**Issue**: System performance depends on SD card write speed. Slow cards may cause buffer overflows.

**Impact**: 
- Class 4 cards may not keep up with 64 ksps sampling
- Buffer overflows may occur with slow cards
- Write failures may increase with poor quality cards

**Workaround**: 
- Use Class 10 or better SD cards
- Use cards from reputable manufacturers
- Format cards properly (FAT32, full format, not quick format)

**Future Improvement**: Adaptive buffering or sample rate throttling.

### 3. WiFi Access Point Security

**Issue**: WiFi access point has no password protection.

**Impact**: 
- Anyone within range can connect
- No authentication on web portal
- Calibration portal is accessible to anyone who knows the URL

**Workaround**: 
- Use system in controlled environment
- Disable WiFi if not needed
- Change default SSID if desired (requires code modification)

**Future Improvement**: 
- Add WPA2 password protection
- Implement web portal authentication
- Add role-based access control

### 4. Maximum Session Duration

**Issue**: Session duration is limited by SD card capacity.

**Impact**: 
- 8GB card: ~66 hours at 64 ksps
- 16GB card: ~133 hours at 64 ksps
- 32GB card: ~266 hours at 64 ksps

**Workaround**: 
- Use larger capacity SD cards for longer sessions
- Monitor SD card free space via web portal
- Plan session durations based on available space

**Future Improvement**: Automatic session splitting or compression.

### 5. Single CSV File Output

**Issue**: System creates one CSV file per session. Very large sessions create very large CSV files.

**Impact**: 
- Large CSV files may be difficult to open in spreadsheet software
- Memory limitations when processing large files
- Slower file operations

**Workaround**: 
- Keep sessions to reasonable durations
- Use data analysis tools that can handle large files
- Process binary files directly if needed

**Future Improvement**: 
- Option to split CSV files by time or size
- Streaming CSV output
- Multiple output formats

### 6. No Real-Time Data Streaming

**Issue**: Data is only available after logging session completes and CSV conversion finishes.

**Impact**: 
- Cannot monitor data in real-time
- Must wait for conversion to view data
- No live feedback during logging

**Workaround**: 
- Use web portal status indicators for system health
- Monitor NeoPixel patterns for status
- Use short test sessions to verify setup

**Future Improvement**: 
- WebSocket-based real-time data streaming
- Live chart updates during logging
- Real-time statistics display

### 7. Calibration Not Persistent in Firmware

**Issue**: Calibration values are stored in NVS but not hardcoded in firmware.

**Impact**: 
- Calibration must be set via web portal
- NVS can be cleared (though unlikely)
- No factory calibration option

**Workaround**: 
- Document calibration values separately
- Save calibration values in multiple locations
- Use calibration portal to verify/update values

**Future Improvement**: 
- Option to hardcode calibration in firmware
- Factory calibration mode
- Calibration backup/restore

### 8. No Data Compression

**Issue**: Binary files are stored uncompressed.

**Impact**: 
- Larger file sizes
- Faster SD card capacity usage
- No space savings

**Workaround**: 
- Use larger capacity SD cards
- Compress files manually after removal
- Delete old sessions regularly

**Future Improvement**: 
- Optional compression for binary files
- Compressed CSV output
- Automatic compression after conversion

### 9. Limited Error Recovery

**Issue**: Some errors require manual intervention (e.g., SD card removal).

**Impact**: 
- System may stop logging on persistent errors
- User must restart system or session
- Data may be lost if session not properly closed

**Workaround**: 
- Monitor system status via web portal
- Check NeoPixel patterns regularly
- Use high-quality SD cards
- Ensure stable power supply

**Future Improvement**: 
- Automatic error recovery mechanisms
- Graceful degradation modes
- Automatic session recovery

### 10. No OTA Updates

**Issue**: Firmware updates require physical access to ESP32.

**Impact**: 
- Must connect via USB to update firmware
- Cannot update remotely
- Requires development environment setup

**Workaround**: 
- Use PlatformIO for firmware updates
- Keep development environment ready
- Document update procedures

**Future Improvement**: 
- OTA firmware update capability
- Web-based firmware upload
- Version management system

## Hardware Limitations

### 1. ADC Sample Rate

**Limitation**: Maximum 64 ksps (hardware limit of MAX11270).

**Impact**: Cannot sample faster than 64 ksps.

**Workaround**: Use appropriate sample rate for application.

### 2. IMU Sample Rate

**Limitation**: Maximum 960 Hz (hardware limit of LSM6DSV16X).

**Impact**: Cannot sample IMU faster than 960 Hz.

**Workaround**: Use appropriate sample rate for application.

### 3. SD Card Interface

**Limitation**: SDMMC 4-bit interface speed.

**Impact**: Write speed limited by SD card and interface.

**Workaround**: Use high-speed SD cards (Class 10+).

### 4. Memory Constraints

**Limitation**: ESP32-S3 has limited RAM and flash.

**Impact**: 
- Ring buffer sizes are limited
- Large arrays may cause issues
- Flash space limits firmware size

**Workaround**: 
- Optimize buffer sizes for application
- Disable debug output in production
- Use external storage for large data

## Workarounds Summary

| Issue | Workaround |
|-------|------------|
| Slow CSV conversion | Keep sessions <30 minutes |
| SD card write speed | Use Class 10+ cards |
| WiFi security | Use in controlled environment |
| Session duration | Use larger SD cards |
| Large CSV files | Use appropriate analysis tools |
| No real-time data | Use status indicators |
| Calibration | Document values separately |
| No compression | Use larger cards or compress manually |
| Error recovery | Monitor system status |
| OTA updates | Use USB connection |

## Reporting Issues

If you encounter an issue not listed here:

1. Check the [Troubleshooting Guide](USER_MANUAL.md#9-troubleshooting)
2. Review serial debug output
3. Check web portal status indicators
4. Open an issue on GitHub with:
   - Description of the problem
   - Steps to reproduce
   - Expected vs. actual behavior
   - System configuration
   - Debug output (if available)

---

**Last Updated:** December 2024

