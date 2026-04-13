/**
 * @file diag_timers.c
 * @brief Diagnostic timer services — CLKIN measurement (TIM8) and DRDY edge counting (TIM3).
 * @details TIM8 counts LTC6903 CLKIN edges on PC6 in external-clock mode 1 (TI1FP1).
 *          At 8.192 MHz the 16-bit counter overflows ~125 times/second; an update
 *          ISR tracks overflows.  diagClkinPoll() computes frequency once per second.
 *
 *          A high-accuracy measurement path (diagClkinMeasureHz) uses DWT CYCCNT
 *          as a SYSCLK-referenced time base for nanosecond-precision over a configurable
 *          window.  This is used by the LTC6903 auto-trim at boot.
 *
 *          TIM3 counts ADS131M02 DRDY falling edges on PB4 (TI1FP1, external-clock
 *          mode 1).  At 64 kHz the 16-bit counter overflows ~once/second; overflow
 *          is tracked via HAL_TIM_PeriodElapsedCallback (TIM3_IRQHandler → main.c).
 *
 * @author Madhu
 * @date   2026-04-12
 * @see    RM0481 §33 (General-purpose timers — TIM3/TIM8)
 * @see    RM0481 §35.4.3 (External clock mode 1)
 * @see    ARMv8-M Architecture Reference Manual (DWT CYCCNT)
 */

#include "diag_timers.h"
#include "debug_ui.h"
#include "debug_uart.h"
#include "tim.h"
#include "main.h"
#include "ux_api.h"
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════
 *  TIM8 / PC6 — CLKIN frequency measurement
 * ═══════════════════════════════════════════════════════════════════ */

#define CLKIN_POLL_MS  1000U

static volatile uint32_t tim8OvfCount;
static uint32_t prevTotal;
static uint32_t prevTick;
static uint32_t cachedHz;

void diagClkinInit(void)
{
    /* CubeMX configured TIM8 with TI2FP2 (PC7) as trigger, but CLKIN is on
     * PC6 (TIM8_CH1).  Override to TI1FP1 at runtime. */
    TIM_SlaveConfigTypeDef slave = {0};
    slave.SlaveMode       = TIM_SLAVEMODE_EXTERNAL1;
    slave.InputTrigger    = TIM_TS_TI1FP1;
    slave.TriggerPolarity = TIM_TRIGGERPOLARITY_RISING;
    slave.TriggerFilter   = 0;
    if (HAL_TIM_SlaveConfigSynchro(&htim8, &slave) != HAL_OK)
    {
        printf("DIAG: TIM8 slave reconfig FAILED\r\n");
        return;
    }

    tim8OvfCount = 0;
    __HAL_TIM_SET_COUNTER(&htim8, 0);

    HAL_NVIC_SetPriority(TIM8_UP_IRQn, 10, 0);
    HAL_NVIC_EnableIRQ(TIM8_UP_IRQn);
    __HAL_TIM_ENABLE_IT(&htim8, TIM_IT_UPDATE);
    __HAL_TIM_ENABLE(&htim8);

    prevTotal = 0;
    prevTick  = HAL_GetTick();
    cachedHz  = 0;
}

/**
 * @brief  TIM8 update (overflow) ISR.
 * @note   CubeMX did not enable TIM8 NVIC, so this handler is defined here
 *         without conflicting with any generated code.
 */
void TIM8_UP_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&htim8, TIM_FLAG_UPDATE))
    {
        __HAL_TIM_CLEAR_FLAG(&htim8, TIM_FLAG_UPDATE);
        tim8OvfCount++;
    }
}

uint32_t diagClkinGetHz(void)
{
    return cachedHz;
}

/**
 * @brief  Atomically snapshot TIM8 edge count and DWT CYCCNT.
 * @param[out] outEdges   Cumulative 32-bit edge count (overflow-extended).
 * @param[out] outCycles  DWT CYCCNT value at the moment of the snapshot.
 * @note   Briefly disables IRQs to prevent TIM8 overflow between counter reads.
 *         Handles the race where an overflow fires between reading ovf and cnt.
 */
static void snapCounters(uint32_t *outEdges, uint32_t *outCycles)
{
    __disable_irq();
    uint32_t ovf = tim8OvfCount;
    uint32_t cnt = __HAL_TIM_GET_COUNTER(&htim8);
    uint32_t cyc = DWT->CYCCNT;
    if (__HAL_TIM_GET_FLAG(&htim8, TIM_FLAG_UPDATE))
    {
        __HAL_TIM_CLEAR_FLAG(&htim8, TIM_FLAG_UPDATE);
        tim8OvfCount++;
        ovf = tim8OvfCount;
        cnt = __HAL_TIM_GET_COUNTER(&htim8);
        cyc = DWT->CYCCNT;
    }
    __enable_irq();

    *outEdges  = ovf * 65536UL + cnt;
    *outCycles = cyc;
}

void diagClkinEnsureDwt(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk))
    {
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

uint32_t diagClkinMeasureHz(uint32_t durationMs)
{
    diagClkinEnsureDwt();

    uint32_t sysclk = HAL_RCC_GetSysClockFreq();

    uint32_t edges0, cyc0;
    snapCounters(&edges0, &cyc0);
    uint32_t tick0 = HAL_GetTick();
    uint32_t end   = tick0 + durationMs;

    uint64_t totalCyc = 0;
    uint32_t prevCyc  = cyc0;

    while ((int32_t)(HAL_GetTick() - end) < 0)
    {
        /* Accumulate DWT deltas periodically to survive 32-bit CYCCNT wraps
         * (wraps every ~17 s at 250 MHz). */
        uint32_t cycNow = DWT->CYCCNT;
        uint32_t dc = cycNow - prevCyc;
        if (dc > sysclk)
        {
            totalCyc += dc;
            prevCyc = cycNow;
        }
    }

    uint32_t edgesN, cycN;
    snapCounters(&edgesN, &cycN);

    totalCyc += (cycN - prevCyc);
    uint64_t totalEdges = (uint64_t)(edgesN - edges0);

    if (totalCyc == 0)
        return 0;

    /* f_CLKIN = edges × SYSCLK / cycles */
    return (uint32_t)(totalEdges * (uint64_t)sysclk / totalCyc);
}

void diagClkinStabilityTest(uint32_t durationS)
{
    diagClkinEnsureDwt();

    uint32_t sysclk = HAL_RCC_GetSysClockFreq();
    double nsPerTick = 1e9 / (double)sysclk;

    printf("\r\n========================================\r\n");
    printf("  CLKIN + HSI STABILITY TEST (%lu s)\r\n",
           (unsigned long)durationS);
    printf("  Timebase: DWT_CYCCNT @ SYSCLK=%lu Hz\r\n",
           (unsigned long)sysclk);
    printf("  Resolution: %.1f ns\r\n", nsPerTick);
    printf("========================================\r\n");

    uint32_t ltcMin = 0xFFFFFFFFUL, ltcMax = 0;
    uint64_t ltcSum = 0;
    uint32_t sysMin = 0xFFFFFFFFUL, sysMax = 0;
    uint64_t sysSum = 0;
    uint32_t nSamples = 0;

    uint32_t edges0, cyc0;
    snapCounters(&edges0, &cyc0);
    uint32_t tick0 = HAL_GetTick();

    uint32_t prevEdges = edges0;
    uint32_t prevCyc   = cyc0;
    uint32_t nextSample = tick0 + 1000;
    uint32_t endTick    = tick0 + durationS * 1000;

    uint64_t totalCyc64 = 0;

    while ((int32_t)(HAL_GetTick() - endTick) < 0)
    {
        ux_system_tasks_run();
        cdcPoll();

        if ((int32_t)(HAL_GetTick() - nextSample) < 0)
            continue;

        uint32_t edgesNow, cycNow;
        snapCounters(&edgesNow, &cycNow);

        uint32_t dEdges = edgesNow - prevEdges;
        uint32_t dCyc   = cycNow   - prevCyc;

        totalCyc64 += dCyc;

        if (dCyc > 0 && dEdges > 0)
        {
            uint32_t ltcHz = (uint32_t)((uint64_t)dEdges * sysclk / dCyc);
            if (ltcHz < ltcMin) ltcMin = ltcHz;
            if (ltcHz > ltcMax) ltcMax = ltcHz;
            ltcSum += ltcHz;

            /* Effective SYSCLK inferred from LTC6903 (assuming 8.192 MHz) */
            uint32_t sysHz = (uint32_t)((uint64_t)dCyc * 8192000ULL / dEdges);
            if (sysHz < sysMin) sysMin = sysHz;
            if (sysHz > sysMax) sysMax = sysHz;
            sysSum += sysHz;

            nSamples++;

            cachedHz = ltcHz;
            uiSetClkinHz(ltcHz);

            if ((nSamples % 10) == 0)
                printf("  ... %lu s\r\n", (unsigned long)nSamples);
        }

        prevEdges = edgesNow;
        prevCyc   = cycNow;
        nextSample += 1000;
    }

    uint32_t edgesN, cycN;
    snapCounters(&edgesN, &cycN);
    uint32_t dCycLast = cycN - prevCyc;
    totalCyc64 += dCycLast;

    uint64_t totalEdges64 = (uint64_t)(edgesN - edges0);

    uint32_t ltcAvg1s = nSamples ? (uint32_t)(ltcSum / nSamples) : 0;
    uint32_t sysAvg1s = nSamples ? (uint32_t)(sysSum / nSamples) : 0;

    uint32_t ltcAvgTotal = (totalCyc64 > 0)
        ? (uint32_t)(totalEdges64 * (uint64_t)sysclk / totalCyc64)
        : 0;
    uint32_t sysAvgTotal = (totalEdges64 > 0)
        ? (uint32_t)(totalCyc64 * 8192000ULL / totalEdges64)
        : 0;

    printf("\r\n--- LTC6903 frequency (SYSCLK as ref) ---\r\n");
    printf("  samples    : %lu\r\n", (unsigned long)nSamples);
    printf("  min        : %lu Hz\r\n", (unsigned long)ltcMin);
    printf("  max        : %lu Hz\r\n", (unsigned long)ltcMax);
    printf("  spread     : %ld Hz\r\n",
           (long)((int32_t)ltcMax - (int32_t)ltcMin));
    printf("  avg (1s)   : %lu Hz\r\n", (unsigned long)ltcAvg1s);
    printf("  avg (full) : %lu Hz\r\n", (unsigned long)ltcAvgTotal);
    printf("  target     : 8192000 Hz\r\n");
    printf("  error      : %+.4f %%\r\n",
           ((double)ltcAvgTotal - 8192000.0) / 8192000.0 * 100.0);

    printf("\r\n--- Effective SYSCLK (LTC6903 as ref, assuming 8.192 MHz) ---\r\n");
    printf("  min        : %lu Hz\r\n", (unsigned long)sysMin);
    printf("  max        : %lu Hz\r\n", (unsigned long)sysMax);
    printf("  spread     : %ld Hz\r\n",
           (long)((int32_t)sysMax - (int32_t)sysMin));
    printf("  avg (1s)   : %lu Hz\r\n", (unsigned long)sysAvg1s);
    printf("  avg (full) : %lu Hz\r\n", (unsigned long)sysAvgTotal);
    printf("  nominal    : %lu Hz\r\n", (unsigned long)sysclk);
    printf("  deviation  : %+.4f %%\r\n",
           ((double)sysAvgTotal - (double)sysclk) / (double)sysclk * 100.0);

    printf("\r\n--- Raw counters ---\r\n");
    printf("  LTC edges  : %lu\r\n", (unsigned long)(uint32_t)totalEdges64);
    printf("  DWT cycles : %lu (high32=%lu)\r\n",
           (unsigned long)(uint32_t)totalCyc64,
           (unsigned long)(uint32_t)(totalCyc64 >> 32));

    printf("\r\nNOTE: No external crystal on this board. Only the\r\n"
           "HSI<->LTC ratio is observable. If one drifts, it\r\n"
           "appears as drift in the other's measurement.\r\n");
    printf("========================================\r\n\r\n");

    prevTotal = edgesN;
    prevTick  = HAL_GetTick();
}

void diagClkinPoll(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t dt  = now - prevTick;
    if (dt < CLKIN_POLL_MS)
        return;

    __disable_irq();
    uint32_t ovf = tim8OvfCount;
    uint32_t cnt = __HAL_TIM_GET_COUNTER(&htim8);
    if (__HAL_TIM_GET_FLAG(&htim8, TIM_FLAG_UPDATE))
    {
        __HAL_TIM_CLEAR_FLAG(&htim8, TIM_FLAG_UPDATE);
        tim8OvfCount++;
        ovf = tim8OvfCount;
        cnt = __HAL_TIM_GET_COUNTER(&htim8);
    }
    __enable_irq();

    uint32_t total = ovf * 65536UL + cnt;
    uint32_t delta = total - prevTotal;

    if (dt > 0)
        cachedHz = (uint32_t)((uint64_t)delta * 1000ULL / dt);

    prevTotal = total;
    prevTick  = now;

    uiSetClkinHz(cachedHz);
}

/* ═══════════════════════════════════════════════════════════════════
 *  TIM3 / PB4 — hardware DRDY edge counter
 *
 *  Mirrors the TIM8 approach: TIM3 in external-clock mode counts
 *  every DRDY falling edge.  At 64 kHz the 16-bit counter overflows
 *  ~once per second; overflow tracked via HAL_TIM_PeriodElapsedCallback.
 * ═══════════════════════════════════════════════════════════════════ */

extern TIM_HandleTypeDef htim3;

static volatile uint32_t tim3OvfCount;

void diagDrdyTim3Overflow(void)
{
    tim3OvfCount++;
}

void diagDrdyInit(void)
{
    /* MX_TIM3_Init() configured TIM3 as input-capture on CH1 (PB4) with
     * prescaler=249.  Reconfigure as external-clock mode 1, falling edge. */
    TIM_SlaveConfigTypeDef slave = {0};
    slave.SlaveMode       = TIM_SLAVEMODE_EXTERNAL1;
    slave.InputTrigger    = TIM_TS_TI1FP1;
    slave.TriggerPolarity = TIM_TRIGGERPOLARITY_FALLING;
    slave.TriggerFilter   = 0;
    if (HAL_TIM_SlaveConfigSynchro(&htim3, &slave) != HAL_OK)
    {
        printf("DIAG: TIM3 slave reconfig FAILED\r\n");
        return;
    }

    __HAL_TIM_SET_PRESCALER(&htim3, 0);
    htim3.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);

    tim3OvfCount = 0;
    __HAL_TIM_SET_COUNTER(&htim3, 0);

    HAL_NVIC_SetPriority(TIM3_IRQn, 8, 0);
    __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_UPDATE);
    __HAL_TIM_ENABLE(&htim3);
}

uint32_t diagDrdyReadEdges(void)
{
    __disable_irq();
    uint32_t ovf = tim3OvfCount;
    uint32_t cnt = __HAL_TIM_GET_COUNTER(&htim3);
    if (__HAL_TIM_GET_FLAG(&htim3, TIM_FLAG_UPDATE))
    {
        ovf = tim3OvfCount + 1;
        cnt = __HAL_TIM_GET_COUNTER(&htim3);
    }
    __enable_irq();
    return ovf * 65536UL + cnt;
}
