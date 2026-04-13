/**
 * @file    battery_monitor.c
 * @brief   Battery voltage, SOC, charger decode, USB sense, and MCU temperature.
 * @details Once-per-second polling of ADC1 (PA1 battery + VSENSE die temperature),
 *          BQ24012 charger GPIOs (PC14/PC15/PB6), and PB1 USB-VBUS sense.
 *          Li-ion SOC estimated via 11-point piecewise-linear OCV table.
 *          LED 0 system state updated via ledStatusSetSys() after each poll.
 *
 *          Upstream: HAL ADC1, GPIO, led_status.
 *          Downstream: debug_ui (VT220 panel), app_state (USB logging gate).
 * @author  Madhu
 * @date    2026-04-12
 * @see     BQ24012 datasheet; RM0481 §21.4.31 (temperature sensor).
 */

#include "battery_monitor.h"
#include "adc.h"
#include "main.h"
#include "led_status.h"
#include "stm32h5xx_hal.h"
#include <stdio.h>

/* ── Fallback pin defines (CubeMX adds these to main.h after IOC regen) ── */

#ifndef CHG_PG_Pin
#define CHG_PG_Pin        GPIO_PIN_7
#define CHG_PG_GPIO_Port  GPIOB
#endif

#ifndef CHG_STAT1_Pin
#define CHG_STAT1_Pin       GPIO_PIN_5
#define CHG_STAT1_GPIO_Port GPIOB
#endif

#ifndef CHG_STAT2_Pin
#define CHG_STAT2_Pin       GPIO_PIN_6
#define CHG_STAT2_GPIO_Port GPIOB
#endif

/* ── Configuration ────────────────────────────────────────────── */

/** @brief  Voltage divider ratio: Vbat * ratio = Vadc.
 *          Default 0.5 (2:1 divider).  Phase 10 loads from config.txt. */
#define BATT_DIVIDER_RATIO  0.5f

/** @brief  ADC full-scale count (12-bit). */
#define ADC_MAX_COUNT       4095u

/** @brief  Number of ADC samples to average for the battery voltage channel.
 *          Reduces noise from the resistive divider and ADC quantisation. */
#define BATT_OVERSAMPLE_N   8u

/** @brief  Sentinel value for SOC when estimation is invalid (e.g. charging). */
#define SOC_UNKNOWN         0xFFu

/** @brief  VREFINT typical voltage (mV) from STM32H562 datasheet.
 *          Factory calibration at 0x08FFF810 is inaccessible (same flash
 *          security issue as TEMPSENSOR_CAL), so we use the datasheet typical.
 * @see    STM32H562 datasheet DS14001 Table 72. */
#define VREFINT_TYPICAL_MV  1212u

/* ── SOC look-up table (OCV → SOC%, 1S Li-ion) ───────────────── */

typedef struct {
    float    voltage;   /**< Open-circuit voltage (V)   */
    uint8_t  socPct;    /**< State-of-charge percentage  */
} socEntry_t;

/** @brief  11-point OCV-to-SOC table, sorted descending by voltage. */
static const socEntry_t SOC_TABLE[] = {
    { 4.20f, 100 },
    { 4.10f,  90 },
    { 4.00f,  80 },
    { 3.90f,  70 },
    { 3.80f,  60 },
    { 3.70f,  50 },
    { 3.60f,  35 },
    { 3.50f,  20 },
    { 3.40f,  10 },
    { 3.30f,   5 },
    { 3.00f,   0 },
};

#define SOC_TABLE_LEN  (sizeof(SOC_TABLE) / sizeof(SOC_TABLE[0]))

/** @brief  Charger state string table, indexed by chargeState_t. */
static const char * const CHARGE_STATE_STR[] = {
    [CHG_BATTERY]  = "BATTERY",
    [CHG_CHARGING] = "CHARGING",
    [CHG_FULL]     = "FULL",
    [CHG_STANDBY]  = "STANDBY",
};

/* ── Cached readings (updated every batteryPoll()) ────────────── */

static float         cachedVoltage;
static uint8_t       cachedSocPct;
static chargeState_t cachedChargeState;
static bool          cachedUsbConnected;
static int16_t       cachedMcuTempX10;
static uint32_t      cachedVddaMv = 3300;
static uint32_t      cachedRawVref;
static uint32_t      cachedRawBatt;
static uint32_t      cachedRawTemp;
static uint32_t      cachedVddCoreMv;

/* ── Internal helpers ─────────────────────────────────────────── */

/**
 * @brief  Perform a single ADC1 conversion on the specified channel.
 * @details Configures the channel, runs a dummy conversion to flush the MUX
 *          settling from the previous channel, then performs the real read.
 *          Uses 640.5-cycle sampling to guarantee >=5 µs for VSENSE/VREFINT
 *          (RM0481 §21.4.31 minimum) at 62.5 MHz ADC clock.
 * @param[in] channel  HAL ADC channel (e.g. ADC_CHANNEL_1, ADC_CHANNEL_TEMPSENSOR).
 * @return Raw 12-bit ADC value, or 0 on timeout.
 */
static uint32_t adc1ReadChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    cfg.Offset       = 0;

    if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK)
        return 0;

    /* Dummy conversion: flush MUX settling / sample-and-hold residual */
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    (void)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    /* Real conversion */
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK)
    {
        HAL_ADC_Stop(&hadc1);
        return 0;
    }
    uint32_t raw = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return raw;
}

/**
 * @brief  Interpolate SOC from the OCV table using battery voltage.
 * @param[in] voltage  Battery voltage (V).
 * @return SOC percentage 0–100.
 */
static uint8_t socFromVoltage(float voltage)
{
    if (voltage >= SOC_TABLE[0].voltage)
        return SOC_TABLE[0].socPct;
    if (voltage <= SOC_TABLE[SOC_TABLE_LEN - 1].voltage)
        return SOC_TABLE[SOC_TABLE_LEN - 1].socPct;

    for (uint32_t i = 0; i < SOC_TABLE_LEN - 1; i++)
    {
        if (voltage >= SOC_TABLE[i + 1].voltage)
        {
            float vHi  = SOC_TABLE[i].voltage;
            float vLo  = SOC_TABLE[i + 1].voltage;
            float sHi  = (float)SOC_TABLE[i].socPct;
            float sLo  = (float)SOC_TABLE[i + 1].socPct;
            float frac = (voltage - vLo) / (vHi - vLo);
            return (uint8_t)(sLo + frac * (sHi - sLo) + 0.5f);
        }
    }
    return 0;
}

/**
 * @brief  Decode BQ24012 charger GPIOs into chargeState_t.
 * @return Decoded charger state.
 * @note   PG is active-low (open-drain, internal pull-up on PC14).
 * @see    BQ24012 datasheet Table 2 — Status output truth table.
 */
static chargeState_t decodeCharger(void)
{
    GPIO_PinState pg    = HAL_GPIO_ReadPin(CHG_PG_GPIO_Port,    CHG_PG_Pin);
    GPIO_PinState stat1 = HAL_GPIO_ReadPin(CHG_STAT1_GPIO_Port, CHG_STAT1_Pin);
    GPIO_PinState stat2 = HAL_GPIO_ReadPin(CHG_STAT2_GPIO_Port, CHG_STAT2_Pin);

    if (pg == GPIO_PIN_SET)
        return CHG_BATTERY;

    if (stat1 == GPIO_PIN_RESET && stat2 == GPIO_PIN_SET)
        return CHG_CHARGING;
    if (stat1 == GPIO_PIN_SET && stat2 == GPIO_PIN_RESET)
        return CHG_FULL;

    return CHG_STANDBY;
}

/**
 * @brief  Compute MCU die temperature from raw VSENSE ADC reading.
 * @details Factory calibration at TEMPSENSOR_CAL1_ADDR (0x08FFF814) is
 *          inaccessible on this STM32H562 — reading it triggers a BusFault
 *          (GTZC / flash block-based security blocks the engineering-bytes
 *          region).  Uses datasheet typical values instead:
 *            Avg_Slope = 2.0 mV/C, V_30 = 620 mV at 30 C.
 *          Accuracy: ~+/-5 C (sufficient for MCU health monitoring).
 * @param[in] rawTemp  12-bit ADC raw value from VSENSE channel.
 * @param[in] vddaMv   Measured VDDA in millivolts (from VREFINT).
 * @return Temperature in tenths of a degree (e.g. 253 = 25.3 C).
 * @see    STM32H562 datasheet DS14001 Table 100 — Analog temperature sensor.
 */
static int16_t computeMcuTempX10(uint32_t rawTemp, uint32_t vddaMv)
{
    float vsenseMv = (float)rawTemp * (float)vddaMv / 4095.0f;
    float tempC    = ((vsenseMv - 620.0f) / 2.0f) + 30.0f;
    return (int16_t)(tempC * 10.0f);
}

/* ── Public API ───────────────────────────────────────────────── */

int batteryInit(void)
{
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
    {
        printf("[BATT] ADC1 calibration FAILED\r\n");
        return -1;
    }
    if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED) != HAL_OK)
        printf("[BATT] ADC2 calibration FAILED (VDDCORE unavailable)\r\n");

    /* Enable VREFINT and TEMPSENSOR internal paths in ADC common register.
     * HAL_ADC_ConfigChannel() does this lazily, but we force it up-front
     * with a generous stabilization delay (RM0481: tSTART = 26 us typ). */
    SET_BIT(ADC12_COMMON->CCR, ADC_CCR_VREFEN | ADC_CCR_TSEN);
    HAL_Delay(1);

    /* Dump ADC parameters for debug */
    uint32_t presc = READ_BIT(ADC12_COMMON->CCR, ADC_CCR_PRESC);
    uint32_t ccr   = ADC12_COMMON->CCR;
    printf("[BATT] ADC1 params: prescaler=0x%02lX CCR=0x%08lX "
           "TSEN=%s VREFEN=%s smpTime=640.5cy\r\n",
           (unsigned long)(presc >> ADC_CCR_PRESC_Pos),
           (unsigned long)ccr,
           (ccr & ADC_CCR_TSEN)   ? "ON" : "OFF",
           (ccr & ADC_CCR_VREFEN) ? "ON" : "OFF");

    batteryPoll();

    printf("[BATT] init OK  VDDA=%lumV VCORE=%lumV (vref_raw=%lu)\r\n",
           (unsigned long)cachedVddaMv, (unsigned long)cachedVddCoreMv,
           (unsigned long)cachedRawVref);
    if (cachedSocPct == SOC_UNKNOWN)
        printf("[BATT]   V=%.2f (raw=%lu) SOC=--- CHG=%s USB=%s\r\n",
               (double)cachedVoltage, (unsigned long)cachedRawBatt,
               CHARGE_STATE_STR[cachedChargeState],
               cachedUsbConnected ? "YES" : "NO");
    else
        printf("[BATT]   V=%.2f (raw=%lu) SOC=%u%% CHG=%s USB=%s\r\n",
               (double)cachedVoltage, (unsigned long)cachedRawBatt,
               cachedSocPct,
               CHARGE_STATE_STR[cachedChargeState],
               cachedUsbConnected ? "YES" : "NO");
    printf("[BATT]   MCU=%.1fC (raw=%lu)\r\n",
           (double)cachedMcuTempX10 / 10.0,
           (unsigned long)cachedRawTemp);
    printf("[BATT]   GPIO: %s\r\n", batteryGetGpioDebugStr());
    return 0;
}

void batteryPoll(void)
{
    /* ── VREFINT → actual VDDA ────────────────────────────────── */
    cachedRawVref = adc1ReadChannel(ADC_CHANNEL_VREFINT);
    if (cachedRawVref > 100)
        cachedVddaMv = (VREFINT_TYPICAL_MV * (uint32_t)ADC_MAX_COUNT) / cachedRawVref;

    /* ── Battery voltage (ADC1 CH1 / PA1, oversampled) ─────────── */
    {
        uint32_t acc = 0;
        for (uint32_t n = 0; n < BATT_OVERSAMPLE_N; n++)
            acc += adc1ReadChannel(ADC_CHANNEL_1);
        cachedRawBatt = acc / BATT_OVERSAMPLE_N;
    }
    float adcVolts = (float)cachedRawBatt * (float)cachedVddaMv
                     / ((float)ADC_MAX_COUNT * 1000.0f);
    cachedVoltage  = adcVolts / BATT_DIVIDER_RATIO;

    /* ── MCU temperature (VSENSE) ─────────────────────────────── */
    cachedRawTemp    = adc1ReadChannel(ADC_CHANNEL_TEMPSENSOR);
    cachedMcuTempX10 = computeMcuTempX10(cachedRawTemp, cachedVddaMv);

    /* ── VDDCORE via ADC2 (single-channel, already configured) ── */
    HAL_ADC_Start(&hadc2);
    if (HAL_ADC_PollForConversion(&hadc2, 10) == HAL_OK)
    {
        uint32_t raw = HAL_ADC_GetValue(&hadc2);
        cachedVddCoreMv = (raw * cachedVddaMv) / ADC_MAX_COUNT;
    }
    HAL_ADC_Stop(&hadc2);

    /* ── Charger status (BQ24012 GPIOs) ───────────────────────── */
    cachedChargeState = decodeCharger();

    /* ── USB VBUS sense (PB1) ─────────────────────────────────── */
    cachedUsbConnected = (HAL_GPIO_ReadPin(USB_SENSE_GPIO_Port,
                                           USB_SENSE_Pin) == GPIO_PIN_SET);

    /* ── SOC estimation (invalid while charging — OCV unreliable) */
    if (cachedChargeState == CHG_CHARGING)
        cachedSocPct = SOC_UNKNOWN;
    else
        cachedSocPct = socFromVoltage(cachedVoltage);

    /* ── LED 0 system state update ────────────────────────────── */
    if (cachedChargeState == CHG_CHARGING)
        ledStatusSetSys(LED_SYS_CHARGING);
    else if (cachedSocPct <= 5)
        ledStatusSetSys(LED_SYS_BATT_CRIT);
    else if (cachedSocPct <= 20)
        ledStatusSetSys(LED_SYS_BATT_LOW);
    else
        ledStatusSetSys(LED_SYS_IDLE);
}

float batteryGetVoltage(void)
{
    return cachedVoltage;
}

uint8_t batteryGetSocPercent(void)
{
    return cachedSocPct;
}

chargeState_t batteryGetChargeState(void)
{
    return cachedChargeState;
}

const char *batteryGetChargeStateStr(void)
{
    if ((unsigned)cachedChargeState < sizeof(CHARGE_STATE_STR) / sizeof(CHARGE_STATE_STR[0]))
        return CHARGE_STATE_STR[cachedChargeState];
    return "???";
}

bool batteryIsUsbConnected(void)
{
    return cachedUsbConnected;
}

int16_t battGetMcuTempX10(void)
{
    return cachedMcuTempX10;
}

uint32_t battGetVddaMv(void)
{
    return cachedVddaMv;
}

uint32_t battGetVddCoreMv(void)
{
    return cachedVddCoreMv;
}

const char *batteryGetSocStr(void)
{
    static char buf[8];
    if (cachedSocPct == SOC_UNKNOWN)
        return "---";
    snprintf(buf, sizeof(buf), "%u%%", cachedSocPct);
    return buf;
}

const char *batteryGetGpioDebugStr(void)
{
    static char buf[24];
    snprintf(buf, sizeof(buf), "PG=%d S1=%d S2=%d",
             HAL_GPIO_ReadPin(CHG_PG_GPIO_Port, CHG_PG_Pin),
             HAL_GPIO_ReadPin(CHG_STAT1_GPIO_Port, CHG_STAT1_Pin),
             HAL_GPIO_ReadPin(CHG_STAT2_GPIO_Port, CHG_STAT2_Pin));
    return buf;
}
