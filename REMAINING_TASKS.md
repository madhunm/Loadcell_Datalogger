# Remaining Implementation Tasks

## ✅ Completed Features

1. **Remote Logging Control** - Fully implemented
   - `/api/logging/start` endpoint
   - `/api/logging/stop` endpoint
   - `/api/logging/status` endpoint
   - Thread-safe integration with main loop

2. **Real-time Data Streaming Backend** - Backend complete
   - WebSocket server on port 81
   - Sample streaming from logger
   - JSON message format

3. **Basic Retry Mechanisms** - Partially implemented
   - SD write retries with exponential backoff (in `logger.cpp`)
   - Max retry attempts configured

4. **Basic Write Buffering** - Implemented
   - 8KB write buffers for ADC and IMU
   - Buffer flushing on full or session end

---

## 🔄 In Progress / Partially Complete

### 1. Real-time Data Visualization UI
**Status:** Backend done, UI missing
- ✅ WebSocket server implemented
- ✅ Data streaming logic in place
- ❌ Web portal UI for charts (Chart.js integration needed)
- ❌ Real-time force/IMU visualization
- ❌ Toggle streaming on/off from UI
- ❌ Chart controls (pause, zoom, etc.)

**Files to modify:**
- `web_portal_preview.html` - Add Chart.js charts
- `src/webconfig.cpp` - Already has streaming logic

---

## ⏳ Not Started / Pending

### 2. Enhanced Error Messages
**Status:** Basic error patterns exist, but no user-friendly messages
- ✅ NeoPixel error patterns (SD, RTC, IMU, ADC, etc.)
- ❌ User-friendly error descriptions on web portal
- ❌ Recovery suggestions
- ❌ Error history/log endpoint
- ❌ Clear troubleshooting steps

**What's needed:**
- Error message mapping (error code → user message)
- Error history storage (last N errors)
- `/api/errors` endpoint
- Error display section in web portal

---

### 3. Statistics and Analytics
**Status:** Placeholder endpoint exists
- ✅ Basic endpoint `/api/statistics` exists
- ❌ Min/max/avg/std dev calculation
- ❌ Per-session statistics
- ❌ Force distribution histograms
- ❌ IMU motion analysis
- ❌ Real-time statistics display

**What's needed:**
- Statistics calculation module
- Running statistics (online algorithm)
- Histogram generation
- Statistics display in web portal

---

### 4. SD Card Write Optimization
**Status:** Basic buffering exists, but not optimized
- ✅ 8KB write buffers
- ❌ Write coalescing (combine multiple small writes)
- ❌ Adaptive flush intervals (based on buffer fill)
- ❌ Larger buffers (currently 8KB, could be 16KB+)
- ❌ Write batching optimization

**What's needed:**
- Increase buffer size (16KB or 32KB)
- Implement write coalescing logic
- Adaptive flush timing (flush when buffer > 75% full)
- Performance monitoring

---

### 5. Power Management
**Status:** Not implemented
- ❌ Deep sleep when idle
- ❌ CPU frequency scaling
- ❌ WiFi power saving modes
- ❌ Auto-shutdown on critical battery
- ❌ Wake-on-button for deep sleep

**What's needed:**
- Deep sleep implementation (ESP32 light sleep/deep sleep)
- CPU frequency scaling API calls
- WiFi power saving mode configuration
- Battery threshold monitoring for shutdown
- Wake-up source configuration

---

### 6. Data Integrity Enhancements
**Status:** CRC32 exists, but no verification
- ✅ CRC32 calculation per file
- ❌ Verify-on-read option
- ❌ Automatic corruption detection
- ❌ Recovery from partial writes
- ❌ File integrity verification endpoint

**What's needed:**
- Read verification function
- Corruption detection during read
- Partial write recovery (truncate corrupted data)
- `/api/integrity/verify` endpoint
- Integrity check on file open

---

### 7. Automatic Retry Mechanisms (Complete)
**Status:** Partially implemented
- ✅ SD write retries (in `logger.cpp`)
- ❌ Peripheral initialization retries
- ❌ Exponential backoff for all failures
- ❌ Retry configuration (max attempts, delays)

**What's needed:**
- Retry wrapper for peripheral init
- Generalized retry mechanism
- Configurable retry parameters
- Retry statistics tracking

---

### 8. Web Portal Enhancements
**Status:** Basic UI exists, enhancements missing
- ✅ Basic web portal with tabs
- ❌ Dark mode toggle
- ❌ Chart customization (colors, scales)
- ❌ Data annotation/marking
- ❌ Comparison view improvements
- ❌ Export data functionality

**What's needed:**
- Dark mode CSS and toggle
- Chart.js customization options
- Annotation UI (mark events in data)
- Comparison view (overlay multiple sessions)
- Data export (CSV, JSON download)

---

## Priority Recommendations

### High Priority (User-Facing)
1. **Real-time Data Visualization UI** - Users can see live data
2. **Enhanced Error Messages** - Better user experience
3. **Statistics and Analytics** - Useful data insights

### Medium Priority (Performance)
4. **SD Card Write Optimization** - Better reliability and performance
5. **Data Integrity Enhancements** - Ensure data quality

### Low Priority (Nice-to-Have)
6. **Power Management** - Battery life extension
7. **Web Portal Enhancements** - UI polish
8. **Automatic Retry Mechanisms** - Already partially done

---

## Estimated Effort

- **Real-time UI:** 4-6 hours
- **Error Messages:** 2-3 hours
- **Statistics:** 3-4 hours
- **SD Optimization:** 2-3 hours
- **Power Management:** 4-5 hours
- **Data Integrity:** 2-3 hours
- **Web Enhancements:** 3-4 hours

**Total remaining:** ~20-28 hours of development

