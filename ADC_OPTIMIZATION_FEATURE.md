# ADC Auto-Optimization Feature

## Overview

The ADC auto-optimization feature automatically finds the optimal PGA gain and sample rate combination that produces the lowest noise. This is particularly useful during initial calibration when you want to maximize signal quality.

## How It Works

### Algorithm

1. **Test All Combinations**: The system tests all combinations of:
   - **PGA Gains**: x1, x2, x4, x8, x16, x32, x64, x128 (8 options)
   - **Sample Rates**: 1k, 2k, 4k, 8k, 16k, 32k, 64k Hz (7 options)
   - **Total**: 56 combinations

2. **Noise Measurement**: For each combination:
   - Changes ADC settings (PGA gain and sample rate)
   - Performs self-calibration
   - Collects samples (default: 5000 samples)
   - Calculates standard deviation (noise level)

3. **Selection**: Chooses the combination with minimum noise

4. **Application**: Automatically applies optimal settings and saves to NVS

### Noise Metric

The optimization uses **standard deviation** of ADC samples as the noise metric:
- Lower standard deviation = lower noise = better signal quality
- Measured in ADC counts
- Assumes loadcell is at zero force (unloaded) during measurement

## Usage

### Prerequisites

1. **Loadcell must be at ZERO FORCE** (unloaded)
2. **System should be in READY state** (not logging)
3. **Allow 2-5 minutes** for optimization to complete

### Steps

1. Access calibration portal: `http://192.168.4.1/cal`
2. Navigate to "ADC Settings" section
3. Scroll to "Auto-Optimize ADC Settings"
4. (Optional) Adjust "Samples per Test" (default: 5000)
   - More samples = more accurate but slower
   - Recommended: 5000-10000 for good balance
5. Click "🚀 Start Optimization"
6. Wait for completion (progress bar shows status)
7. Review results:
   - Optimal Gain (e.g., x4)
   - Optimal Sample Rate (e.g., 32000 Hz)
   - Noise Level (e.g., 45.23 ADC counts)
8. Click "💾 Save All Settings" to persist

### Important Notes

- **Optimization stops the ADC sampling task** - this is intentional to prevent interference
- **Sampling task will restart automatically** when you start a new logging session
- **Settings are saved to NVS** automatically after optimization
- **Do not remove SD card** during optimization

## Technical Details

### Implementation

**Functions Added:**
- `adcChangeSettings()` - Change PGA gain and sample rate dynamically
- `adcMeasureNoise()` - Collect samples and calculate standard deviation
- `adcOptimizeSettings()` - Test all combinations and find optimal

**Web Endpoint:**
- `POST /cal/optimize?samples=<num>` - Start optimization

**UI Location:**
- Calibration portal (`/cal`) → ADC Settings section

### Default Test Parameters

**Gains Tested:**
- All 8 PGA gains: x1, x2, x4, x8, x16, x32, x64, x128

**Sample Rates Tested:**
- 1,000 Hz
- 2,000 Hz
- 4,000 Hz
- 8,000 Hz
- 16,000 Hz
- 32,000 Hz
- 64,000 Hz

**Samples per Test:**
- Default: 5,000 samples
- Configurable: 1,000 - 50,000 samples

### Timing

**Per Combination:**
- Settings change: ~200ms (including self-calibration)
- Sample collection: ~(samples / sample_rate) seconds
- Example: 5000 samples at 32k Hz = ~156ms
- **Total per combination: ~400ms**

**Total Optimization Time:**
- 56 combinations × 400ms = ~22 seconds (minimum)
- With 5000 samples: ~2-3 minutes
- With 10000 samples: ~4-5 minutes

### Memory Usage

- **Stack allocation**: Used for ≤1000 samples
- **Heap allocation**: Used for >1000 samples
- **Maximum**: 50,000 samples × 4 bytes = 200 KB (temporary)

## Recommendations

### When to Use

✅ **Recommended:**
- Initial system setup
- After changing loadcell or signal conditioning
- When experiencing high noise levels
- During 5-point calibration procedure

❌ **Not Recommended:**
- During active logging sessions
- When loadcell is under load
- If system is unstable

### Best Practices

1. **Ensure Zero Force**: 
   - Remove all weights/forces from loadcell
   - Allow system to stabilize (5 minutes warm-up)
   - Check that ADC readings are stable

2. **Use Adequate Samples**:
   - Minimum: 2000 samples
   - Recommended: 5000 samples
   - Maximum accuracy: 10000 samples

3. **Run Multiple Times**:
   - Run optimization 2-3 times
   - Compare results (should be consistent)
   - Use most common result

4. **Verify Results**:
   - Check that optimal settings make sense
   - Very high gain (x128) may indicate signal too weak
   - Very low gain (x1) may indicate signal too strong

## Expected Results

### Typical Optimal Settings

**For Standard Loadcells:**
- **PGA Gain**: x4 to x16 (most common: x4 or x8)
- **Sample Rate**: 16k to 64k Hz (most common: 32k or 64k Hz)
- **Noise Level**: 20-100 ADC counts (depends on loadcell and signal conditioning)

### Interpretation

**Low Noise (<50 ADC counts):**
- Excellent signal quality
- Optimal settings found
- System ready for calibration

**Medium Noise (50-150 ADC counts):**
- Good signal quality
- May benefit from better signal conditioning
- Suitable for most applications

**High Noise (>150 ADC counts):**
- Check loadcell connections
- Verify signal conditioning
- Check for electrical interference
- Consider different PGA gain

## Troubleshooting

### Optimization Fails

**Symptoms:**
- Error message in web portal
- Serial output shows "Optimization failed"

**Solutions:**
- Check ADC connections (SPI)
- Verify loadcell is connected
- Ensure system is in READY state
- Check serial output for specific error

### Inconsistent Results

**Symptoms:**
- Different optimal settings on each run
- Large variation in noise levels

**Solutions:**
- Increase samples per test (10000+)
- Ensure loadcell is truly at zero force
- Check for vibrations or interference
- Allow longer settling time (10 minutes)

### Optimal Settings Don't Make Sense

**Symptoms:**
- Very high gain (x128) selected
- Very low gain (x1) selected
- Very low sample rate selected

**Solutions:**
- Verify loadcell signal level
- Check signal conditioning circuit
- Manually test different settings
- Review loadcell specifications

## Integration with 5-Point Calibration

### Recommended Workflow

1. **System Setup**: Ensure all hardware connected
2. **Auto-Optimize**: Run optimization to find optimal settings
3. **Save Settings**: Save optimal ADC settings
4. **5-Point Calibration**: 
   - Apply known weights (0N, 10N, 20N, 50N, 100N)
   - Record ADC values at each point
   - Calculate scaling factor and offset
5. **Save Calibration**: Save loadcell calibration values
6. **Verify**: Test with known forces

### Benefits

- **Optimal Signal Quality**: Maximum signal-to-noise ratio
- **Consistent Results**: Same settings every time
- **Automated Process**: No manual tuning required
- **Data-Driven**: Based on actual noise measurements

## API Reference

### C++ Functions

```cpp
// Change ADC settings dynamically
bool adcChangeSettings(AdcPgaGain pgaGain, uint32_t sampleRate);

// Measure noise (standard deviation)
bool adcMeasureNoise(size_t numSamples, float &noiseStdDev, uint32_t timeoutMs = 5000);

// Optimize settings (test all combinations)
bool adcOptimizeSettings(
    const AdcPgaGain *testGains, size_t numGains,
    const uint32_t *testRates, size_t numRates,
    size_t samplesPerTest,
    AdcOptimizationResult &result);
```

### Web API

**Endpoint:** `POST /cal/optimize`

**Parameters:**
- `samples` (optional): Number of samples per test (100-50000, default: 5000)

**Response:**
```json
{
  "success": true,
  "optimalGain": 2,
  "optimalSampleRate": 32000,
  "noiseLevel": 45.23
}
```

**Error Response:**
```json
{
  "success": false,
  "error": "Optimization failed"
}
```

## Future Enhancements

### Potential Improvements

1. **SNR-Based Optimization**: 
   - Measure signal-to-noise ratio instead of just noise
   - Requires known signal level

2. **Custom Test Ranges**:
   - Allow user to specify which gains/rates to test
   - Faster optimization for known constraints

3. **Progressive Optimization**:
   - Start with coarse search, then fine-tune
   - Faster convergence

4. **Multi-Point Optimization**:
   - Test at multiple force levels
   - Find settings optimal across full range

5. **Real-Time Feedback**:
   - Show progress for each combination
   - Display intermediate results

---

**Version:** 1.0  
**Date:** December 2024

