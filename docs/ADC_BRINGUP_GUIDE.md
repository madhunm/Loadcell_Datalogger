# MAX11270 ADC Bring-Up Guide

Step-by-step debugging guide for the loadcell ADC interface.

---

## Hardware Checklist

Before software debugging, verify hardware connections:

### Power Rails
- [ ] 3.3V supply to MAX11270 AVDD, DVDD
- [ ] 3.3V supply to loadcell excitation (E+ / E-)
- [ ] GND connections solid

### SPI Connections (check with multimeter/scope)
| Signal | ESP32 Pin | MAX11270 Pin | Notes |
|--------|-----------|--------------|-------|
| MISO   | GPIO 12   | DOUT         | Data from ADC |
| MOSI   | GPIO 13   | DIN          | Data to ADC |
| SCK    | GPIO 18   | SCLK         | Clock (4 MHz) |
| CS     | GPIO 17   | CS           | Active LOW |

### Control Signals
| Signal | ESP32 Pin | State Required | Notes |
|--------|-----------|----------------|-------|
| SYNC   | GPIO 14   | **HIGH**       | Must be HIGH for conversions! |
| RSTB   | GPIO 15   | HIGH           | LOW = reset |
| RDYB   | GPIO 16   | Toggles        | LOW = data ready |

### Loadcell Wiring (from certificate)
| Wire Color | Connection | Notes |
|------------|------------|-------|
| Black      | E+ (excitation +) | Connect to 3.3V |
| Green      | E- (excitation -) | Connect to GND |
| Red        | S+ (signal +) | To ADC AIN+ |
| White      | S- (signal -) | To ADC AIN- |
| Shield     | GND | Optional |

---

## Step 1: Verify SPI Communication

First, check if we can talk to the ADC at all.

### Test via Serial Monitor

After flashing, check the boot log for:
```
[Init] ADC MAX11270... OK (raw: XXXXX, XX.X uV)
```

If you see `FAILED`, SPI communication is not working.

### Test via Web API

Navigate to: `http://<device-ip>/api/diag/adc`

**Expected Response:**
```json
{
  "registers": {
    "STAT1": "0x01",    // Bit 0 = RDY (data ready)
    "CTRL1": "0x00",    // Should show config
    "CTRL2": "0x07",    // Bits 2:0 = gain (0x07 = 128x)
    "CTRL3": "0x00"
  },
  "gpio": {
    "RDYB": 0,          // 0 = data ready, 1 = busy
    "SYNC": 1,          // MUST BE 1 for conversions!
    "RSTB": 1           // 1 = not in reset
  },
  "config": {
    "gain": 128,
    "rate_hz": 64000
  },
  "readings": [123456, 123460, 123455],
  "voltages_uV": [2256.3, 2257.0, 2256.1],
  "adc_present": true
}
```

### What to Check:

| Issue | Symptom | Fix |
|-------|---------|-----|
| No SPI | `adc_present: false`, all registers 0xFF or 0x00 | Check wiring, CS pin |
| SYNC wrong | `SYNC: 0` | Code bug - SYNC must be HIGH |
| Not converting | `RDYB: 1` always | Check SYNC, rate config |
| All zeros | `readings: [0, 0, 0]` | No input signal (Step 2) |

---

## Step 2: Verify Input Signal

If SPI works but readings are zero, the input signal chain needs checking.

### Check Loadcell Excitation

With multimeter, measure:
- **E+ to E-**: Should be ~3.3V (your excitation voltage)
- **S+ to S-**: Should be a few millivolts (the output signal)

**At no load:** Signal should be near zero balance (~18 µV at 3.3V)
**With load:** Signal increases proportionally

### Expected Signal Levels

For TC023L0-000025 at 3.3V excitation:

| Load | Output (µV) | ADC Raw (~) |
|------|-------------|-------------|
| 0 kg | ~0-20 | ~0-8500 |
| 500 kg | ~1128 | ~484,000 |
| 1000 kg | ~2256 | ~968,000 |
| 2000 kg | ~4528 | ~1,943,000 |

### Quick Test: Short AIN+ to AIN-

Temporarily short the ADC inputs together:
- Reading should be very close to 0 (within ±100 counts)
- If you see large values when shorted, there's an offset problem

---

## Step 3: Verify Gain Setting

The PGA gain affects input range:

| Gain | Input Range | For Your Loadcell |
|------|-------------|-------------------|
| 1x   | ±2.5V | Way too large |
| 128x | ±19.5mV | **Correct for 3.3V excitation** |

### Check CTRL2 Register

In `/api/diag/adc`, check `CTRL2`:
- `0x07` = gain 128x ✓
- `0x00` = gain 1x (signal will be tiny!)

### If Gain is Wrong

The gain is set in `main.cpp`:
```cpp
adcConfig.gain = MAX11270::Gain::X128;  // Should be X128
```

---

## Step 4: Run Self-Calibration

The ADC has internal calibration to remove offset/gain errors.

### Via Serial Command (add to your code):

```cpp
// In setup() or a test function:
Serial.println("Running ADC self-calibration...");
MAX11270::reset();  // This runs self-cal automatically
delay(500);

// Read calibration registers
uint32_t scoc = MAX11270::readRegister(MAX11270::Register::SCOC);
uint32_t scgc = MAX11270::readRegister(MAX11270::Register::SCGC);
Serial.printf("Self-cal offset: 0x%06X\n", scoc);
Serial.printf("Self-cal gain: 0x%06X\n", scgc);
```

---

## Step 5: Verify Continuous Mode

For data logging, the ADC runs in continuous mode with DMA.

### Check DRDY Interrupt

The RDYB pin should toggle at the sample rate (64 kHz = every 15.6 µs).

**In `/api/diag/adc`:**
- If `RDYB` alternates between readings, interrupt is working
- If always 1, ADC isn't converting
- If always 0, data isn't being read fast enough

### Enable Verbose Logging

In `main.cpp`, set:
```cpp
#define DEBUG_VERBOSE 1
```

This will print detailed ADC status every 10 seconds.

---

## Step 6: Check Signal Path

### Complete Signal Chain

```
Loadcell → AIN+/AIN- → PGA (128x) → ADC (24-bit) → SPI → ESP32 → Ring Buffer
    ↓
  3.3V excitation
```

### Measure at Each Point

1. **Loadcell output (S+/S-)**: Use precision multimeter
   - No load: ~5-20 µV
   - Full load: ~4500 µV

2. **After PGA (internal)**: Cannot measure directly
   - Calculated: input × 128

3. **ADC counts**: From `/api/diag/adc` or serial
   - Formula: `counts = (Vin × 128) / (2.5V / 2^23)`

---

## Step 7: Validate Calibration Math

### Raw to Microvolts Formula

```cpp
// In max11270.cpp:
float rawToMicrovolts(int32_t raw) {
    uint8_t gain = 128;  // PGA gain
    float fullScale = 2.5 / gain;  // = 0.01953125V
    float resolution = fullScale / (1 << 23);  // = 2.33 nV/count
    return raw * resolution * 1000000.0f;  // Convert to µV
}
```

### Verify with Known Values

| Raw ADC | Expected µV | Calculated |
|---------|-------------|------------|
| 0 | 0 | 0 |
| 1,000,000 | 2328.3 | Check! |
| 8,388,607 | 19531.2 | Full scale |

---

## Step 8: Continuous Mode Data Integrity

### Check for Sample Loss

In `/api/stream` or serial output, monitor:
- `samplesAcquired` - should increase steadily
- `samplesDropped` - should be 0!
- `dmaQueueFull` - should be 0!

### If Dropping Samples

1. SD card too slow → reduce sample rate or use faster card
2. Buffer too small → increase ring buffer size
3. ISR too slow → check for blocking code

---

## Diagnostic API Endpoints

| Endpoint | Purpose |
|----------|---------|
| `/api/diag/adc` | ADC registers, GPIO states, test readings |
| `/api/live` | Single ADC reading with voltage |
| `/api/stream` | Real-time SSE stream |
| `/api/status` | System status including ADC |

---

## Common Issues & Solutions

### Issue: All readings are 0

**Causes:**
1. Loadcell not powered (E+/E- not connected to 3.3V/GND)
2. Signal wires swapped or disconnected
3. SYNC pin LOW (must be HIGH)
4. ADC in power-down mode

**Debug:**
```
1. Check /api/diag/adc → gpio.SYNC must be 1
2. Measure 3.3V at loadcell E+ to E-
3. Measure signal at S+ to S- (should be µV range)
4. Short AIN+ to AIN- temporarily → should read ~0
```

### Issue: Readings are stuck at same value

**Causes:**
1. DRDY interrupt not firing
2. DMA not completing
3. Continuous mode not started

**Debug:**
```
1. Check serial log for "[MAX11270] First DRDY received"
2. Check /api/diag/adc multiple times → readings should differ slightly
```

### Issue: Readings are very noisy

**Causes:**
1. Poor grounding
2. Long unshielded wires
3. Electrical interference
4. Gain too high

**Debug:**
```
1. Add filtering capacitors near ADC
2. Use shielded cable for loadcell
3. Try lower gain (64x instead of 128x)
```

### Issue: Readings drift over time

**Causes:**
1. Temperature changes
2. Loadcell creep
3. ADC self-heating

**Debug:**
```
1. Run self-calibration periodically
2. Allow warm-up time (5-10 minutes)
3. Use RTC temperature for compensation
```

---

## Quick Test Sequence

Run these tests in order:

```
1. Flash firmware
2. Open http://<ip>/api/diag/adc
   → Check adc_present: true
   → Check gpio.SYNC: 1
   → Check registers are not all 0xFF

3. Open http://<ip>/api/live
   → Note raw_adc and voltage_uV values

4. Apply known load to loadcell
   → Refresh /api/live
   → Verify readings change proportionally

5. Open http://<ip>/admin.html
   → Check calibration points display
   → Click "Save to Device"
   → Refresh page → data should persist

6. Start logging
   → Monitor /api/stream for data flow
   → Check no dropped samples
```

---

## Success Criteria

✅ `/api/diag/adc` shows `adc_present: true`
✅ `gpio.SYNC` = 1, `gpio.RSTB` = 1
✅ Raw readings change when load applied
✅ Voltage readings match expected values (±10%)
✅ Calibration saves and loads correctly
✅ Continuous mode runs without dropped samples

