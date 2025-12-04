# Enhancement Proposals for Flawless Datalogger Performance

## 🔴 Critical Enhancements (High Priority)

### 1. **Write Failure Retry Logic with Exponential Backoff**
**Current Issue:** Write failures are logged but execution continues. No retry mechanism means transient SD card issues cause permanent data loss.

**Proposal:**
- Implement retry logic with exponential backoff (1ms, 2ms, 4ms, 8ms)
- Track consecutive write failures per file
- After 5 consecutive failures, stop session and set error pattern
- Add write failure counters to status endpoint

**Impact:** Prevents data loss from transient SD card issues (vibration, power fluctuations)

---

### 2. **SD Card Space Pre-Check**
**Current Issue:** No check for available space before starting session. Could fill card mid-session.

**Proposal:**
- Check available space in `loggerStartSession()`
- Require minimum free space (e.g., 10MB or 1 minute of data)
- Calculate estimated space needed based on sample rates
- Reject session start if insufficient space

**Impact:** Prevents session failures mid-logging due to full card

---

### 3. **Buffer Overflow Prevention & Early Warning**
**Current Issue:** Overflows just drop samples silently. No warning until it's too late.

**Proposal:**
- Add buffer fill level monitoring (warn at 75%, critical at 90%)
- If buffer > 90% full, increase processing priority temporarily
- Log overflow events with timestamps
- Add overflow rate to status endpoint (overflows per second)
- Consider dynamic buffer size increase if memory allows

**Impact:** Prevents data loss by catching issues before overflow

---

### 4. **Write Failure Counter & Session Termination**
**Current Issue:** Write failures continue indefinitely without stopping session.

**Proposal:**
- Track consecutive write failures per file (ADC and IMU separately)
- After 10 consecutive failures, stop session gracefully
- Set error NeoPixel pattern
- Log final statistics (samples written, failures, duration)

**Impact:** Prevents wasting time logging to a failed SD card

---

### 5. **Data Integrity: CRC/Checksum for Binary Files**
**Current Issue:** No way to verify data integrity after logging.

**Proposal:**
- Add CRC32 checksum to file headers
- Calculate CRC during write, store in header at session end
- Add CRC verification in CSV conversion
- Optionally: per-record checksums for critical data

**Impact:** Enables detection of corrupted data files

---

## 🟡 Important Enhancements (Medium Priority)

### 6. **Configuration Persistence to NVS**
**Current Issue:** Web config changes are lost on reboot.

**Proposal:**
- Save validated web config to NVS on successful POST
- Load saved config on startup
- Use saved config as default for new sessions
- Add "Reset to Defaults" button in web interface

**Impact:** User settings persist across reboots

---

### 7. **Buffer Health Monitoring & Auto-Tuning**
**Current Issue:** No visibility into buffer health during operation.

**Proposal:**
- Track buffer fill level over time
- Calculate average fill percentage
- If consistently > 50%, log warning
- If consistently > 80%, consider increasing processing rate
- Add buffer health metrics to status endpoint

**Impact:** Proactive detection of performance issues

---

### 8. **Graceful Degradation on Sensor Failure**
**Current Issue:** If one sensor fails, entire logging stops.

**Proposal:**
- Continue logging with working sensor if one fails
- Log sensor failure to separate error log
- Set partial error pattern (e.g., red/yellow for partial failure)
- Allow session to continue with available data

**Impact:** Maximizes data collection even with partial failures

---

### 9. **File Size Limits & Automatic Session Splitting**
**Current Issue:** Files can grow indefinitely, risking SD card issues.

**Proposal:**
- Set maximum file size (e.g., 100MB or 1GB)
- Automatically split session into multiple files when limit reached
- Number files sequentially: `baseName_001_ADC.bin`, `baseName_002_ADC.bin`
- Update CSV conversion to handle multiple files

**Impact:** Prevents file system issues with very large files

---

### 10. **Performance Monitoring: Actual vs Expected Sample Rates**
**Current Issue:** No verification that 64 ksps is actually maintained.

**Proposal:**
- Track actual sample arrival rate (samples per second)
- Compare to expected rate (64 ksps for ADC, 960 Hz for IMU)
- Log warning if actual rate < 95% of expected
- Add rate metrics to status endpoint
- Consider auto-adjusting processing limits if rate drops

**Impact:** Ensures data quality by verifying sampling rates

---

## 🟢 Nice-to-Have Enhancements (Low Priority)

### 11. **Memory Leak Prevention & String Optimization**
**Current Issue:** String concatenation in hot paths could cause fragmentation.

**Proposal:**
- Replace String concatenation with `snprintf()` where possible
- Use stack-allocated buffers for temporary strings
- Monitor free heap over time
- Add heap fragmentation metrics to status

**Impact:** Prevents memory issues during long logging sessions

---

### 12. **Task Priority Optimization**
**Current Issue:** Task priorities may not be optimal for all scenarios.

**Proposal:**
- Review and document priority hierarchy
- Consider dynamic priority adjustment based on buffer levels
- Ensure web server has adequate priority for responsiveness
- Add priority information to status endpoint

**Impact:** Better real-time performance under load

---

### 13. **Enhanced Error Recovery**
**Current Issue:** Limited recovery from peripheral failures.

**Proposal:**
- Implement retry logic for peripheral initialization
- Add "reinitialize" command via web interface
- Graceful handling of I2C bus errors
- Automatic retry for SD card mount failures

**Impact:** Better resilience to transient hardware issues

---

### 14. **Session Statistics & Metadata**
**Current Issue:** Limited information about completed sessions.

**Proposal:**
- Track session statistics: duration, samples written, file sizes
- Store metadata file alongside binary files
- Include statistics in CSV header
- Add session list endpoint to web interface

**Impact:** Better post-processing and data management

---

### 15. **Watchdog Coverage Verification**
**Current Issue:** Not all critical tasks may be covered by watchdog.

**Proposal:**
- Audit all FreeRTOS tasks for watchdog coverage
- Ensure web server task is covered
- Add watchdog reset verification (log if reset occurs)
- Consider separate watchdog for each core

**Impact:** Better system reliability and fault detection

---

## Implementation Priority Recommendation

**Phase 1 (Critical - Implement First):**
1. Write Failure Retry Logic (#1)
2. SD Card Space Pre-Check (#2)
3. Write Failure Counter & Session Termination (#4)
4. Buffer Overflow Prevention (#3)

**Phase 2 (Important - Implement Next):**
5. Configuration Persistence (#6)
6. Buffer Health Monitoring (#7)
7. Data Integrity CRC (#5)
8. Performance Monitoring (#10)

**Phase 3 (Nice-to-Have - Future):**
9. Graceful Degradation (#8)
10. File Size Limits (#9)
11. Enhanced Error Recovery (#13)
12. Session Statistics (#14)

---

## Testing Recommendations

For each enhancement:
1. **Unit Tests:** Test retry logic, space checking, overflow detection
2. **Integration Tests:** Test full logging session with various failure scenarios
3. **Stress Tests:** Test with full buffers, slow SD card, sensor failures
4. **Long Duration Tests:** Test for memory leaks, watchdog stability
5. **Field Tests:** Test in actual operating conditions

---

## Metrics to Track

- Write failure rate (failures per 1000 writes)
- Buffer overflow rate (overflows per second)
- Actual vs expected sample rates
- Memory usage over time
- SD card write speed (bytes per second)
- Session success rate
- Average session duration
- File size distribution
