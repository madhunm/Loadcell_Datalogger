#pragma once

// Tare: number of frames to average for one-point tare (500 Hz -> 200 frames = 0.4 s)
static constexpr unsigned TARE_N = 200;

// Low battery: set FLG_LOW_BATT when below these (SOC 10000 = 100.00%, mV)
static constexpr uint16_t LOW_BATT_SOC_CENTI = 2000;  // 20.00%
static constexpr uint16_t LOW_BATT_MV = 3400;
