#pragma once

// Loadcell calibration constants
// Update these values based on your specific loadcell calibration
// 
// Scaling Factor: Converts ADC counts to Newtons (N)
//   Formula: Force (N) = (ADC_Code - ADC_Baseline) × Scaling_Factor
//
// ADC Baseline: The ADC value that corresponds to 0N (zero force)
//   For 24-bit signed ADC: typically 2^23 = 8,388,608 (mid-point)
//
// Example calibration:
//   If 3G spike = 20kN and corresponds to +3,000,000 ADC counts above baseline:
//   Scaling_Factor = 20000 N / 3000000 counts = 0.00667 N/ADC

#define LOADCELL_SCALING_FACTOR_N_PER_ADC  0.00667f  // N per ADC count
#define LOADCELL_ADC_BASELINE               8388608   // 24-bit ADC mid-point (0N reference)

