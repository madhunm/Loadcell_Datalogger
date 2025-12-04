# Multi-Point SNR Optimization Guide

## Overview

The multi-point SNR optimization feature tests ADC settings (PGA gain and sample rate) at **5 different load points** and finds the optimal combination that maximizes **weighted Signal-to-Noise Ratio** across the full measurement range.

## Why Multi-Point Optimization?

**Advantages over single-point optimization:**
- ✅ **Works across full range**: Optimizes for all load levels, not just one
- ✅ **Accounts for load-dependent effects**: Some noise sources vary with load
- ✅ **More robust solution**: Settings work well at all measurement points
- ✅ **Better for 5-point calibration**: Aligns with your calibration procedure

## How It Works

### Algorithm

1. **Measure SNR at 5 load points:**
   - Point 1: 0N (unloaded) - baseline noise
   - Point 2: 25% full scale
   - Point 3: 50% full scale (most important)
   - Point 4: 75% full scale
   - Point 5: 100% full scale

2. **For each PGA gain/sample rate combination:**
   - Test at all 5 load points
   - Calculate SNR at each point
   - Compute weighted average: `Weighted_SNR = Σ(SNR_i × weight_i)`

3. **Select optimal combination:**
   - Choose settings with maximum weighted SNR
   - These settings work best across the full range

### Weight Distribution

**Default weights:**
- Point 1 (0N): 15% - Baseline noise reference
- Point 2 (25%): 20% - Low load performance
- Point 3 (50%): 30% - **Most important** (typical operating point)
- Point 4 (75%): 20% - High load performance
- Point 5 (100%): 15% - Maximum load

**Rationale:** 50% load gets highest weight because it's the most representative operating point.

## Usage Workflow

### Step 1: Prepare System

1. Ensure loadcell is connected and stable
2. Access calibration portal: `http://192.168.4.1/cal`
3. Navigate to "ADC Settings" section
4. Scroll to "Multi-Point SNR Optimization"

### Step 2: Measure Baseline (Point 1)

1. **Ensure loadcell is at ZERO FORCE** (completely unloaded)
2. Click "📏 Measure" button for Point 1
3. System measures baseline ADC value and noise
4. Wait for measurement to complete (~5-10 seconds)
5. Point 1 shows ✅ with SNR value

### Step 3: Loading Phase (Points 2-5)

**Apply weights in INCREASING order:**

1. **Point 2 (25% full scale):**
   - Apply weight = 25% of your maximum expected load
   - Example: If max is 100N, apply 25N
   - Wait for loadcell to stabilize (5-10 seconds)
   - Click "📏 Measure" for Point 2
   - Wait for measurement

2. **Point 3 (50% full scale):**
   - Add more weight to reach 50% of maximum
   - Example: Add 25N more (total 50N)
   - Wait for stabilization
   - Click "📏 Measure" for Point 3

3. **Point 4 (75% full scale):**
   - Add more weight to reach 75% of maximum
   - Example: Add 25N more (total 75N)
   - Wait for stabilization
   - Click "📏 Measure" for Point 4

4. **Point 5 (100% full scale):**
   - Add final weight to reach 100% of maximum
   - Example: Add 25N more (total 100N)
   - Wait for stabilization
   - Click "📏 Measure" for Point 5

### Step 4: Unloading Phase (Optional Verification)

**Remove weights in DECREASING order:**

- Remove weight to 75% → verify Point 4 still good
- Remove weight to 50% → verify Point 3 still good
- Remove weight to 25% → verify Point 2 still good
- Remove all weights → verify Point 1 still good

**Note:** You don't need to re-measure during unloading unless you want to verify consistency.

### Step 5: Run Optimization

1. **Verify all 5 points are measured:**
   - All points should show ✅ with SNR values
   - If any point is missing, measure it first

2. **Adjust settings (optional):**
   - "Samples per Test": Default 5000 (recommended: 5000-10000)
   - Baseline ADC values: Should be set automatically, but can be adjusted

3. **Click "🚀 Start Multi-Point Optimization"**
   - System tests all 56 combinations (8 gains × 7 rates)
   - For each combination, measures SNR at all 5 points
   - Calculates weighted SNR
   - Selects optimal combination

4. **Wait for completion:**
   - Progress bar shows status
   - Takes 3-8 minutes (depending on samples per test)
   - Do not remove SD card or interrupt

### Step 6: Review Results

**Results displayed:**
- Optimal PGA Gain (e.g., x4)
- Optimal Sample Rate (e.g., 32000 Hz)
- Weighted SNR (e.g., 45.23 dB)
- Per-point SNR values

**Settings automatically updated:**
- ADC Sample Rate field updated
- PGA Gain dropdown updated

### Step 7: Save Settings

1. Click "💾 Save All Settings" button
2. Settings saved to NVS (persistent)
3. Ready for use in logging sessions

## Example Workflow

**Scenario:** Loadcell with 100N full scale capacity

1. **Point 1 (0N):**
   - No weights applied
   - Measure → SNR: 35.2 dB

2. **Point 2 (25N):**
   - Apply 25N weight
   - Wait 10 seconds
   - Measure → SNR: 42.5 dB

3. **Point 3 (50N):**
   - Add 25N more (total 50N)
   - Wait 10 seconds
   - Measure → SNR: 48.3 dB

4. **Point 4 (75N):**
   - Add 25N more (total 75N)
   - Wait 10 seconds
   - Measure → SNR: 46.8 dB

5. **Point 5 (100N):**
   - Add 25N more (total 100N)
   - Wait 10 seconds
   - Measure → SNR: 44.1 dB

6. **Optimization:**
   - System tests all combinations
   - Finds optimal: Gain=x4, Rate=32kHz
   - Weighted SNR: 45.6 dB

7. **Save:**
   - Click "Save All Settings"
   - Settings persisted

## Tips and Best Practices

### Loading Sequence

✅ **DO:**
- Apply weights in increasing order
- Wait for stabilization (5-10 seconds) before measuring
- Use consistent weights (calibrated weights if possible)
- Measure all 5 points before optimizing

❌ **DON'T:**
- Skip points (measure all 5)
- Measure too quickly (allow stabilization)
- Use inconsistent weights
- Interrupt during optimization

### Weight Selection

**Recommended distribution:**
- Use actual percentages of your full scale
- If full scale is 100N: 0N, 25N, 50N, 75N, 100N
- If full scale is 50kN: 0N, 12.5kN, 25kN, 37.5kN, 50kN

**Alternative (if you don't know full scale):**
- Use evenly spaced weights: 0%, 25%, 50%, 75%, 100% of maximum you'll use
- System will still optimize correctly

### Baseline ADC Values

**Automatic:**
- System uses measured baseline from Point 1
- Usually correct (24-bit mid-point: 8,388,608)

**Manual adjustment:**
- If you know exact zero-force ADC value, enter it
- Useful if loadcell has offset

### Samples per Test

**Recommendations:**
- **5000 samples**: Good balance (default, ~3-5 minutes)
- **10000 samples**: More accurate (~6-8 minutes)
- **2000 samples**: Faster but less accurate (~2-3 minutes)

**Trade-off:**
- More samples = more accurate but slower
- Fewer samples = faster but less reliable

## Troubleshooting

### "Please measure all load points"

**Problem:** Optimization button disabled, some points not measured

**Solution:**
- Click "📏 Measure" for each unmeasured point
- Wait for each measurement to complete
- All 5 points must show ✅ before optimizing

### Inconsistent Results

**Problem:** Different optimal settings on each run

**Solution:**
- Increase "Samples per Test" (use 10000)
- Ensure loadcell is stable before each measurement
- Check for vibrations or interference
- Allow longer stabilization time (15-20 seconds)

### Measurement Fails

**Problem:** "Measurement failed" error

**Solution:**
- Check ADC connections (SPI)
- Verify loadcell is connected
- Ensure system is in READY state
- Check serial output for specific error
- Try reducing "Samples per Test"

### Optimization Takes Too Long

**Problem:** Optimization runs for >10 minutes

**Solution:**
- Reduce "Samples per Test" (use 2000-3000)
- This is normal for 5000+ samples (56 combinations × 5 points = 280 measurements)

## Technical Details

### SNR Calculation

For each load point:
```
Signal_RMS = √(Σ(ADC_i - Baseline)² / N)
Noise_RMS = √(Σ(ADC_i - Mean)² / N)
SNR_dB = 20 × log₁₀(Signal_RMS / Noise_RMS)
```

### Weighted SNR

```
Weighted_SNR = Σ(SNR_i × weight_i) / Σ(weight_i)
```

### Optimization Process

1. For each PGA gain (8 options):
   - For each sample rate (7 options):
     - Change ADC settings
     - Wait for settling (200ms)
     - For each load point (5 points):
       - Measure SNR
     - Calculate weighted SNR
     - Compare with best so far

2. Select combination with maximum weighted SNR

**Total measurements:** 8 gains × 7 rates × 5 points = 280 measurements

## Comparison: Single-Point vs Multi-Point

| Feature | Single-Point | Multi-Point |
|---------|-------------|-------------|
| **Measurement points** | 1 (unloaded or single load) | 5 (across full range) |
| **Optimization metric** | Noise (unloaded) or SNR (single load) | Weighted SNR (all loads) |
| **Time** | 2-5 minutes | 3-8 minutes |
| **Accuracy** | Good for specific load | Excellent across full range |
| **Robustness** | Settings may not work at all loads | Settings work at all loads |
| **Best for** | Quick setup, single operating point | Production calibration, wide range |

## Next Steps

After optimization:
1. **Save settings** (click "Save All Settings")
2. **Proceed with 5-point calibration** (calculate scale and offset)
3. **Verify with test measurements** (apply known weights)
4. **Start logging sessions** with optimal settings

---

**The multi-point optimization ensures your ADC settings work optimally across the entire measurement range, providing the best possible signal quality for your 5-point calibration procedure.**

