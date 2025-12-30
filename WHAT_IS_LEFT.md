# What's Left to Implement

## ✅ **COMPLETED** (Recently Implemented)

1. **Real-time Data Visualization UI** ✅
   - Chart.js charts for force and IMU data
   - WebSocket streaming integration
   - Real-time updates at 20Hz
   - Toggle streaming on/off from UI
   - Latest values display

2. **Enhanced Error Messages** ✅
   - Error tracking system with user-friendly descriptions
   - Recovery suggestions for each error type
   - Error history (last 10 errors)
   - `/api/errors` endpoint for error monitoring

3. **Statistics and Analytics** ✅ (Basic)
   - Enhanced statistics handler
   - Latest sample tracking
   - Write statistics included
   - Note: Full min/max/avg/std dev from historical data not yet implemented

4. **SD Card Write Optimization** ✅
   - Increased write buffers from 8KB to **16KB**
   - **Adaptive flush at 75%** buffer fill
   - Improved write performance

5. **Automatic Retry Mechanisms** ✅
   - Retry logic for **all peripheral initialization**
   - Exponential backoff (100ms, 200ms, 400ms)
   - Up to 3 retry attempts per peripheral
   - SD write retries already existed

6. **Remote Logging Control** ✅
   - `/api/logging/start` endpoint
   - `/api/logging/stop` endpoint
   - `/api/logging/status` endpoint

---

## ⏳ **REMAINING TASKS**

### 1. **Power Management** (Not Started)
**Priority:** Low (Battery Life Extension)

- ❌ Deep sleep when idle
- ❌ CPU frequency scaling
- ❌ WiFi power saving modes
- ❌ Auto-shutdown on critical battery
- ❌ Wake-on-button for deep sleep

**What's needed:**
- Deep sleep implementation (ESP32 light sleep/deep sleep)
- CPU frequency scaling API calls (`setCpuFrequencyMhz()`)
- WiFi power saving mode configuration
- Battery threshold monitoring for shutdown
- Wake-up source configuration

**Estimated Effort:** 4-5 hours

---

### 2. **Data Integrity Enhancements** (Not Started)
**Priority:** Medium (Data Quality)

- ✅ CRC32 calculation per file (already exists)
- ❌ Verify-on-read option
- ❌ Automatic corruption detection
- ❌ Recovery from partial writes
- ❌ File integrity verification endpoint

**What's needed:**
- Read verification function (recalculate CRC32 on read)
- Corruption detection during read
- Partial write recovery (truncate corrupted data)
- `/api/integrity/verify` endpoint
- Integrity check on file open

**Estimated Effort:** 2-3 hours

---

### 3. **Advanced Statistics** (Partially Complete)
**Priority:** Medium (Data Insights)

- ✅ Basic statistics endpoint
- ✅ Latest sample tracking
- ❌ **Min/max/avg/std dev from historical data** (needs ring buffer sampling)
- ❌ Per-session statistics
- ❌ Force distribution histograms
- ❌ IMU motion analysis
- ❌ Real-time statistics display in web portal

**What's needed:**
- Statistics calculation from ring buffer samples (non-destructive)
- Running statistics (online algorithm)
- Histogram generation
- Statistics display section in web portal UI

**Estimated Effort:** 3-4 hours

---

### 4. **Web Portal Enhancements** (Partially Complete)
**Priority:** Low (UI Polish)

- ✅ Basic web portal with tabs
- ✅ Real-time charts (just added)
- ❌ **Dark mode toggle**
- ❌ Chart customization (colors, scales, axes)
- ❌ Data annotation/marking
- ❌ Comparison view improvements
- ❌ Export data functionality (CSV/JSON download)

**What's needed:**
- Dark mode CSS and toggle button
- Chart.js customization options UI
- Annotation UI (mark events in data)
- Comparison view (overlay multiple sessions)
- Data export buttons (CSV, JSON download)

**Estimated Effort:** 3-4 hours

---

### 5. **Write Coalescing** (Not Started)
**Priority:** Low (Performance Optimization)

- ✅ Larger buffers (16KB)
- ✅ Adaptive flush
- ❌ **Write coalescing** (combine multiple small writes)
- ❌ Write batching optimization

**What's needed:**
- Logic to combine multiple small writes into larger blocks
- Batch write optimization for better SD card performance

**Estimated Effort:** 2-3 hours

---

## Summary

### Completed: **6 out of 10 major features** ✅
### Remaining: **4 features** (mostly low priority)

**Total Remaining Effort:** ~12-18 hours

### Priority Order:
1. **Data Integrity** (Medium) - 2-3 hours
2. **Advanced Statistics** (Medium) - 3-4 hours  
3. **Power Management** (Low) - 4-5 hours
4. **Web Portal Enhancements** (Low) - 3-4 hours
5. **Write Coalescing** (Low) - 2-3 hours

---

## Notes

- Most critical features are **completed**
- Remaining items are mostly **nice-to-have** enhancements
- The system is **fully functional** with all core features working
- Remaining tasks focus on **optimization** and **polish**

