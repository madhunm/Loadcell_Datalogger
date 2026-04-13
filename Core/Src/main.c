/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "cordic.h"
#include "crc.h"
#include "fmac.h"
#include "gpdma.h"
#include "icache.h"
#include "rtc.h"
#include "sdmmc.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb.h"
#include "app_usbx.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* Uncomment to stream $IMU CSV at 20 Hz for the Python visualizer */
#define VIZ_STREAM

#include "debug_uart.h"
#include "debug_ui.h"
#include "diag_timers.h"
#include "osc_ltc6903.h"
#include "adc_ads131m02.h"
#include "imu_lsm6dsv.h"
#include "neopixel.h"
#include "led_status.h"
#include "battery_monitor.h"
#include "app_state.h"
#include "calibration.h"
#include "data_processing.h"
#include "circular_buffer.h"
#include "sdmmc_fatfs.h"
#include "log_record.h"
#include "fatfs.h"
#include "ux_device_cdc_acm.h"
#include "ux_dcd_stm32.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/** MBR partition 2 entry starts at 0x1CE; type byte is at +4 → 0x1D2 (set to 0x83 Linux after format). */
#define MBR_PART2_TYPE_OFFSET  0x1D2U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

static sdSession_t g_sdSession;
static uint32_t    s_logBtnDebounceMs;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  Override the SDMMC1 clock divider at runtime.
 * @param[in] div  Clock divider value (10-bit, applied to CLKCR.CLKDIV).
 * @note   Used to switch from the safe init speed to 25 MHz production speed
 *         after FatFS mount succeeds.
 * @see    RM0481 §55.5.1 (SDMMC_CLKCR register)
 */
static void sdSetClkdiv(uint32_t div)
{
    MODIFY_REG(hsd1.Instance->CLKCR, SDMMC_CLKCR_CLKDIV, div & 0x3FFU);
}

/** Work buffer for f_fdisk / f_mkfs (must not be on stack). */
static BYTE fmtWork[4096];

/**
 * @brief Create LOGGER README on partition 0 if it does not exist.
 */
static void writeReadmeIfAbsent(void)
{
    FIL       f;
    FRESULT   fr;
    UINT      bw;
    static const char kReadme[] =
        "This SD card is used by the H562 Parachute Datalogger.\r\n"
        "Data files appear here automatically after each logging session.\r\n"
        "Do not modify or delete any files on this card.\r\n"
        "If the card stops working, contact your equipment provider.\r\n";

    fr = f_open(&f, "0:README.txt", FA_CREATE_NEW | FA_WRITE);
    if (fr != FR_OK)
        return;

    f_write(&f, kReadme, sizeof(kReadme) - 1U, &bw);
    f_close(&f);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPDMA1_Init();
  MX_ICACHE_Init();
  /* MX_SDMMC1_SD_Init(); */    /* Moved to USER CODE 2 for ordered init */
  MX_USART1_UART_Init();
  /* Remaining peripherals called from USER CODE BEGIN 2 in specific order */
  /* MX_USB_PCD_Init(); */
  /* MX_SPI1_Init(); */
  /* MX_ADC1_Init(); */
  /* MX_TIM2_Init(); */
  /* MX_TIM3_Init(); */
  /* MX_TIM8_Init(); */
  /* MX_RTC_Init(); */
  /* MX_CORDIC_Init(); */
  /* MX_CRC_Init(); */
  /* MX_FMAC_Init(); */
  /* MX_USBX_Init(); */
  MX_ADC2_Init();
  /* USER CODE BEGIN 2 */

  printf("\r\n[BOOT] SYSCLK=%luMHz HCLK=%luM SPI1=12.5M SPI2=10.0M\r\n",
         HAL_RCC_GetSysClockFreq() / 1000000UL,
         HAL_RCC_GetHCLKFreq() / 1000000UL);

  /* Suppress DRDY interrupts during init.
   * CubeMX enables EXTI2 in gpio.c; we disable it here and re-enable after
   * all SPI1 peripherals (LTC6903, ADS131M02) are configured, preventing
   * a DRDY edge from triggering DMA on a half-configured SPI bus.
   * @see .cursor/plans/snazzy-petting-mountain.md GOTCHA 8 */
  HAL_NVIC_DisableIRQ(EXTI2_IRQn);

  /* ── Peripheral Inits ─────────────────────────────────────────── */
  MX_USB_PCD_Init();
  MX_SPI1_Init();

  MX_TIM8_Init();

  MX_ADC1_Init();                /* Phase 9b: battery monitor */
  MX_TIM2_Init();                /* Phase 9a: NeoPixel WS2812 */
  MX_TIM3_Init();               /* Phase 7: DRDY frequency */
  /* MX_RTC_Init(); */           /* Later */
  /* MX_CORDIC_Init(); */
  /* MX_CRC_Init(); */
  /* MX_FMAC_Init(); */

  /* ── NVIC Priority Overrides ──────────────────────────────────────
   * CubeMX sets every IRQ to priority 0.  Override here (USER CODE section,
   * CubeMX-safe) so the 64 kHz ADC DRDY hot path (EXTI2 + SPI1 DMA CH0/CH1)
   * at priority 0 is never preempted by lower-priority peripherals.
   * STM32H5: 4 priority bits → range 0–15, lower = higher priority.
   * @see .cursor/plans/snazzy-petting-mountain.md BLOCKER 5 */
  /* Prio 0: EXTI2 + GPDMA CH0/CH1 — already 0 from CubeMX */
  HAL_NVIC_SetPriority(SPI1_IRQn, 1, 0);
  HAL_NVIC_SetPriority(SDMMC1_IRQn, 5, 0);
  HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 6, 0);
  HAL_NVIC_SetPriority(USART1_IRQn, 7, 0);
  HAL_NVIC_SetPriority(EXTI4_IRQn, 8, 0);
  HAL_NVIC_SetPriority(SPI2_IRQn, 4, 0);
  HAL_NVIC_SetPriority(TIM2_IRQn, 10, 0);  /* NeoPixel: cosmetic, lowest tier */

  /* ── Phase 9a: NeoPixel Status LEDs ──────────────────────────── */
  if (neoInit() == 0)
    ledStatusInit();            /* LED 0 = RED HEARTBEAT (boot), LED 1 = OFF */

  /* ── Chip-select housekeeping ─────────────────────────────────── */
  HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LTC_CS_GPIO_Port, LTC_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);

  /* ── Phase 5: LTC6903 oscillator (8.192 MHz CLKIN) ────────────── */
  ltc6903Init();

  /* ── Phase 5: TIM8 CLKIN frequency counter ─────────────────────── */
  diagClkinInit();

  /* ── Phase 5: Auto-trim LTC6903 DAC to minimize CLKIN error ──── */
  ltc6903AutoTrim();

  /* ── Phase 6: ADS131M02 ADC basic communication ─────────────── */
  {
    int adcRet = ads131m02Init();
    ledStatusSetSub(LED_SUB_ADC,
                    (adcRet == 0) ? LED_LEVEL_OK : LED_LEVEL_ERROR);
  }

  /* ── Phase 8: LSM6DSV IMU (SPI2, blocking, 5.0 MHz) ─────────── */
  {
    int imuRet = imuInit();
    ledStatusSetSub(LED_SUB_IMU,
                    (imuRet == 0) ? LED_LEVEL_OK : LED_LEVEL_ERROR);
  }
  imuCalibrate();
  {
    uiSetImuGrav(imuGetGravAxis());
    float ca[3], cg[3];
    imuGetCalOffsets(&ca[0], &ca[1], &ca[2], &cg[0], &cg[1], &cg[2]);
    uiSetImuCal(ca[0], ca[1], ca[2], cg[0], cg[1], cg[2]);
    uiSetCalSource("SFLP+SW");
  }

  /* ── Phase 9b: Battery + charger + MCU temp ──────────────────── */
  batteryInit();

  /* ── USB CDC ──────────────────────────────────────────────────── */
  MX_USBX_Init();
  ux_dcd_stm32_initialize((ULONG)USB_DRD_FS, (ULONG)&hpcd_USB_DRD_FS);
  HAL_PCD_Start(&hpcd_USB_DRD_FS);

  {
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < 5000)
    {
      ux_system_tasks_run();
      cdcPoll();
    }
  }

  {
    const char *usbStr = cdc_acm_get_instance() ? "CDC" : "---";
    printf("[USB] CDC %s after enumeration wait\r\n",
           cdc_acm_get_instance() ? "ACTIVE" : "NOT CONNECTED");
    uiSetUsbStatus(usbStr);
  }

  /* ── SDMMC + FatFS dual-volume init ─────────────────────────────── */
  bool sdReady = false;

  MX_SDMMC1_SD_Init();
  HAL_NVIC_SetPriority(SDMMC1_IRQn, 5, 0);
  {
      HAL_SD_CardStateTypeDef cstate = HAL_SD_GetCardState(&hsd1);
      if (cstate != HAL_SD_CARD_TRANSFER)
      {
          printf("[SD] init failed, card state=%lu\r\n", (unsigned long)cstate);
          uiSetSdStatus("ERROR");
          ledStatusSetSub(LED_SUB_SD, LED_LEVEL_ERROR);
          ledStatusSetSys(LED_SYS_ERROR);
          goto sdDone;
      }
      HAL_SD_CardInfoTypeDef ci;
      HAL_SD_GetCardInfo(&hsd1, &ci);
      (void)ci;
  }

  MX_FATFS_Init();

  {
      FRESULT fr = sdMountAll();

      if (fr == FR_OK)
      {
          sdReady = true;
          printf("[SD] SYSCAL mounted\r\n");
      }
      else if (fr == FR_NO_FILESYSTEM)
      {
          /* FatFs R0.15 w/patch1 — f_fdisk: percentage table (≤100 = %). */
          const LBA_t partSizes[] = { 50, 50, 0, 0 };
          printf("[SD] No filesystem — partitioning (FatFs R0.15 w/patch1)...\r\n");

          fr = f_fdisk(0, partSizes, fmtWork);
          if (fr != FR_OK)
          {
              printf("[SD] FATAL: f_fdisk failed %d\r\n", (int)fr);
              ledStatusSetSys(LED_SYS_ERROR);
          }
          else
          {
              /* FatFs R0.15 w/patch1 — f_mkfs: MKFS_PARM variant. */
              const MKFS_PARM mkOpt = { FM_FAT32, 0U, 0U, 0U, 0U };

              fr = f_mkfs("0:", &mkOpt, fmtWork, sizeof(fmtWork));
              if (fr != FR_OK)
                  printf("[SD] FATAL: f_mkfs 0: failed %d\r\n", (int)fr);

              if (fr == FR_OK)
              {
                  fr = f_mkfs("1:", &mkOpt, fmtWork, sizeof(fmtWork));
                  if (fr != FR_OK)
                      printf("[SD] FATAL: f_mkfs 1: failed %d\r\n", (int)fr);
              }

              /* Patch MBR partition-2 type to 0x83 (Linux native) so Explorer often skips SYSCAL.
               * FatFS mounts via VolToPart[] partition index, not this byte. Not used if GPT (not this build). */
              if (fr == FR_OK)
              {
                  DRESULT dr = disk_read(0, fmtWork, 0, 1);
                  if (dr != RES_OK)
                  {
                      printf("[SD] FATAL: MBR read LBA0 dr=%d\r\n", (int)dr);
                      fr = FR_DISK_ERR;
                  }
                  else
                  {
                      fmtWork[MBR_PART2_TYPE_OFFSET] = 0x83U;
                      dr = disk_write(0, fmtWork, 0, 1);
                      if (dr != RES_OK)
                      {
                          printf("[SD] FATAL: MBR write LBA0 dr=%d\r\n", (int)dr);
                          fr = FR_DISK_ERR;
                      }
                  }
              }

              if (fr == FR_OK)
                  fr = sdMountAll();

              if (fr == FR_OK)
              {
                  f_setlabel("0:LOGGER");
                  f_setlabel("1:SYSCAL");
                  printf("[SD] format complete: LOGGER + SYSCAL\r\n");
                  printf("[SD] SYSCAL mounted\r\n");
                  sdReady = true;
              }
              else
                  ledStatusSetSys(LED_SYS_ERROR);
          }
      }
      else
      {
          printf("[SD] FATAL: sdMountAll error %d\r\n", (int)fr);
          ledStatusSetSys(LED_SYS_ERROR);
      }
  }

  if (sdReady)
  {
      DWORD   freClust;
      FATFS  *fsPtr = NULL;
      FRESULT fres  = f_getfree("0:", &freClust, &fsPtr);

      if (fres != FR_OK || fsPtr == NULL)
      {
          printf("[SD] FAIL f_getfree %d\r\n", (int)fres);
          uiSetSdStatus("ERROR");
          ledStatusSetSub(LED_SUB_SD, LED_LEVEL_ERROR);
          sdReady = false;
      }
      else
      {
          uint32_t freeMb = (uint32_t)((uint64_t)freClust * fsPtr->csize * 512UL
                                        / (1024UL * 1024UL));
          printf("[SD] OK, free=%lu MB\r\n", (unsigned long)freeMb);
          uiSetSdStatus("READY");
          ledStatusSetSub(LED_SUB_SD, LED_LEVEL_OK);
          sdSetClkdiv(1U);
      }
  }

sdDone:

  /* ── Phase 10b: README, cell scan, .cal load ───────────────────── */
  if (sdReady)
  {
      writeReadmeIfAbsent();
      calScanFiles();
      uint32_t selSerial = calSelectViaUi();
      calibrationLoadFromCal(selSerial);
  }

  bool calLoaded = (calibrationGetSource() == CAL_SRC_SD_FILE);

  if (calLoaded)
  {
      ads131m02SetGain(calibrationGet()->adcGainCh1,
                       calibrationGet()->adcGainCh2);
      dpInit(calibrationGet());
      ringInit(ringDescBin());
      ringInit(ringDescCsv());

      {
          char     calBuf[24];
          uint32_t sn = calibrationGetSerial();
          snprintf(calBuf, sizeof(calBuf), "SN:%lu", (unsigned long)sn);
          uiSetCalSource(calBuf);
      }

      printf("[P10b] cal=SD SN=%lu sens=%.6f corr=%.6f crc=%u\r\n",
             (unsigned long)calibrationGetSerial(),
             (double)calibrationGet()->sensitivityUvPerN,
             (double)calibrationGet()->cellCorrFactor,
             calibrationGet()->enableAdcCrc);
  }
  else
  {
      printf("[CAL] FAULT: no valid cal loaded, acquisition halted\r\n");
  }

  /* ── Re-enable EXTI2 now that all SPI1 init is done ────────────── */
  __HAL_GPIO_EXTI_CLEAR_IT(ADC_DRDY_Pin);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  /* ── Phase 7: TIM3/PB4 hardware DRDY edge counter ─────────────── */
  diagDrdyInit();

  /* ── Phase 7: start continuous DMA capture ────────────────────── */
  if (calLoaded)
      ads131m02StartContinuous();

  uiSetState(calLoaded ? "RUN" : "FAULT");
  if (calLoaded)
      ledStatusSetSys(LED_SYS_IDLE);  /* LED 0 → RED SOLID (Power ON) */

  /* ── Draw UI ──────────────────────────────────────────────────── */
#ifndef VIZ_STREAM
  uiDrawPanel();
#endif
  uiLog("SYSCLK:%luM LTC:0x%04X CLKIN:%luHz DAC:%u",
         HAL_RCC_GetSysClockFreq() / 1000000UL, ltc6903GetWord(),
         (unsigned long)ltc6903GetMeasuredHz(), ltc6903GetDac());

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* 1 s UART heartbeat — raw, zero dependencies */
    {
      static uint32_t lastAlive;
      uint32_t nowA = HAL_GetTick();
      if (nowA - lastAlive >= 1000)
      {
        lastAlive = nowA;
        char ab[4] = { '.', '\0' };
        HAL_UART_Transmit(&huart1, (uint8_t *)ab, 1, 5);
      }
    }

    /* ── NeoPixel LED status update (~20 Hz) ─────────────────────── */
    {
      static uint32_t lastLed;
      uint32_t nowLed = HAL_GetTick();
      if (nowLed - lastLed >= LED_UPDATE_INTERVAL_MS)
      {
        lastLed = nowLed;
        ledStatusUpdate();
      }
    }

    ux_system_tasks_run();
    cdcPoll();
    diagClkinPoll();

    /* ── Phase 11: logging FSM, force→ring, SD flush (needs valid cal + SD) ─ */
    if (calLoaded && sdReady)
    {
      appState_t stLog = appStateGet();

      if (appStateConsumeButtonPress())
      {
        uint32_t nowBtn = HAL_GetTick();
        if ((nowBtn - s_logBtnDebounceMs) >= 300U)
        {
          s_logBtnDebounceMs = nowBtn;

          if (stLog == STATE_IDLE && appStateCanStartLogging())
          {
            dpTare();
            FRESULT sfr = sdSessionOpen(&g_sdSession);
            if (sfr != FR_OK)
            {
              printf("[LOG] sdSessionOpen failed: %d\r\n", (int)sfr);
              appStateSet(STATE_ERROR);
              ledStatusSetSub(LED_SUB_SD, LED_LEVEL_ERROR);
              ledStatusSetSys(LED_SYS_ERROR);
            }
            else
            {
              g_loggingActive = true;
              appStateSet(STATE_LOGGING);
              ledStatusSetLogging(true);
              ledStatusSetSub(LED_SUB_LOGGER, LED_LEVEL_OK);
            }
          }
          else if (stLog == STATE_LOGGING)
          {
            g_loggingActive = false;
            appStateSet(STATE_STOPPING);
          }
        }
      }

      if (g_dpPendingForceRecord)
      {
        g_dpPendingForceRecord = 0;
        dpFillImu(&g_dpStagedForce);
        if (calibrationGet()->enableAdcCrc)
          g_dpStagedForce.crc16 = crc16Ccitt((const uint8_t *)&g_dpStagedForce, 30);
        else
          g_dpStagedForce.crc16 = 0x0000;

        if (g_loggingActive)
        {
          if (ringPush(ringDescBin(), &g_dpStagedForce, sizeof(g_dpStagedForce))
              == sizeof(g_dpStagedForce))
            g_forcePushCount++;

          dpFormatForceCsvLine(&g_dpStagedForce);
          if (ringPush(ringDescCsv(), g_dpStagedCsv, g_dpStagedCsvLen)
              == (uint32_t)g_dpStagedCsvLen)
            g_sdSession.csvCount++;
        }
      }

      stLog = appStateGet();
      if ((stLog == STATE_LOGGING || stLog == STATE_STOPPING) && g_sdSession.isOpen)
      {
        while (ringUsed(ringDescBin()) >= 4096U)
        {
          const uint8_t *p;
          uint32_t       avail = ringDrainContiguous(ringDescBin(), &p);
          uint32_t       chunk = avail > 4096U ? 4096U : avail;
          if (chunk == 0U)
            break;
          if (sdSessionWriteBinChunk(&g_sdSession, p, (UINT)chunk) != FR_OK)
          {
            g_loggingActive = false;
            (void)sdSessionClose(&g_sdSession);
            appStateSet(STATE_ERROR);
            ledStatusSetSys(LED_SYS_ERROR);
            break;
          }
          ringAdvanceTail(ringDescBin(), chunk);
        }

        {
          uint32_t usedBin = ringUsed(ringDescBin());
          if (usedBin > g_sdSession.ringPeakUsed)
            g_sdSession.ringPeakUsed = usedBin;

          static bool inPressure;
          uint32_t pct = usedBin * 100U / RING_BIN_SIZE;
          if (pct > 75U)
          {
            if (!inPressure)
            {
              inPressure = true;
              g_sdSession.pressureEvents++;
              printf("[LOG] ring pressure >75%%\r\n");
            }
          }
          else if (pct < 50U)
          {
            inPressure = false;
          }
        }

        if (appStateGet() == STATE_STOPPING)
        {
          while (ringUsed(ringDescBin()) > 0U)
          {
            const uint8_t *p;
            uint32_t       avail = ringDrainContiguous(ringDescBin(), &p);
            if (avail == 0U)
              break;
            uint32_t chunk = avail > 4096U ? 4096U : avail;
            if (sdSessionWriteBinChunk(&g_sdSession, p, (UINT)chunk) != FR_OK)
            {
              g_loggingActive = false;
              (void)sdSessionClose(&g_sdSession);
              appStateSet(STATE_ERROR);
              ledStatusSetSys(LED_SYS_ERROR);
              break;
            }
            ringAdvanceTail(ringDescBin(), chunk);
          }
        }

        while (ringUsed(ringDescCsv()) > 0U)
        {
          const uint8_t *p;
          uint32_t       avail = ringDrainContiguous(ringDescCsv(), &p);
          if (avail == 0U)
            break;
          if (sdSessionWriteCsvChunk(&g_sdSession, p, (UINT)avail) != FR_OK)
          {
            g_loggingActive = false;
            (void)sdSessionClose(&g_sdSession);
            appStateSet(STATE_ERROR);
            ledStatusSetSys(LED_SYS_ERROR);
            break;
          }
          ringAdvanceTail(ringDescCsv(), avail);
        }

        sdSessionTrySync(&g_sdSession);

        {
          static uint32_t s_lastMetaMs;
          uint32_t        nowM = HAL_GetTick();
          if (appStateGet() == STATE_LOGGING && g_loggingActive
              && ((nowM - s_lastMetaMs) >= 1000U))
          {
            s_lastMetaMs = nowM;
            binMetaRecord_t meta;
            memset(&meta, 0, sizeof(meta));
            meta.type          = REC_TYPE_META;
            meta.secondNum     = (uint16_t)(((nowM - g_sdSession.sessionStartTick) / 1000U) & 0xFFFFU);
            meta.clkinHz       = diagClkinGetHz();
            meta.mcuTempX10    = battGetMcuTempX10();
            meta.batteryMv     = (uint16_t)(batteryGetVoltage() * 1000.0f);
            meta.drdyTotal     = ads131m02GetStats()->drdyCount;
            meta.missTotal     = ads131m02GetStats()->missCount;
            meta.overflowTotal = g_binRing.overflow;
            meta.adsStatus     = dpGetLastAdsStatus();
            meta.crc16         = crc16Ccitt((uint8_t *)&meta, offsetof(binMetaRecord_t, crc16));
            if (ringPush(ringDescBin(), &meta, sizeof(meta)) == sizeof(meta))
              g_sdSession.metaCount++;
          }
        }

        {
          static uint32_t s_lastLogUartMs;
          uint32_t        nowL = HAL_GetTick();
          if (appStateGet() == STATE_LOGGING && ((nowL - s_lastLogUartMs) >= 1000U))
          {
            s_lastLogUartMs = nowL;
            printf("LOG: t=%lus adc=%lu force=%lu ovf=%lu ring=%lu%% stall=%lums\r\n",
                   (unsigned long)((nowL - g_sdSession.sessionStartTick) / 1000U),
                   (unsigned long)(g_adcPushCount - g_sdSession.adcPushBase),
                   (unsigned long)(g_forcePushCount - g_sdSession.forcePushBase),
                   (unsigned long)(g_binRing.overflow - g_sdSession.overflowBase),
                   (unsigned long)(ringUsed(ringDescBin()) * 100U / RING_BIN_SIZE),
                   (unsigned long)g_sdSession.stallMaxMs);
          }
        }

        if (appStateGet() == STATE_STOPPING && (ringUsed(ringDescBin()) == 0U)
            && (ringUsed(ringDescCsv()) == 0U))
        {
          (void)sdSessionClose(&g_sdSession);
          appStateSet(STATE_IDLE);
          ledStatusSetLogging(false);
        }
      }
    }

    if (calLoaded)
    {

      /* ── Phase 10: force UI update at ~10 Hz ───────────────────── */
      {
        static uint32_t lastForceUi;
        uint32_t nowFui = HAL_GetTick();
        if (nowFui - lastForceUi >= 100)
        {
          lastForceUi = nowFui;
          uiSetForce(dpGetLatestForceN());
        }
      }
    }

#ifndef VIZ_STREAM
    uiUpdateFields();
#endif
    uiProcessInput();

    /* ── ADC stats (1 Hz, UI feed only — no serial print) ─────── */
    {
      static uint32_t lastAdcTick;
      static uint32_t prevDrdySw, prevDrdyHw, prevDma, prevMiss;
      uint32_t now = HAL_GetTick();
      if (now - lastAdcTick >= 1000)
      {
        lastAdcTick = now;
        const volatile adsDmaStats_t *s = ads131m02GetStats();
        uint32_t hw = diagDrdyReadEdges();
        uint32_t swD   = s->drdyCount  - prevDrdySw;
        uint32_t hwD   = hw             - prevDrdyHw;
        uint32_t dmaD  = s->dmaCount   - prevDma;
        uint32_t missD = s->missCount  - prevMiss;
        prevDrdySw = s->drdyCount;
        prevDrdyHw = hw;
        prevDma    = s->dmaCount;
        prevMiss   = s->missCount;

        uiSetDrdyHz(swD);
        uiSetAdcCounts(s->ch0Latest, s->ch1Latest);
        uiSetAdcRing(swD, dmaD, missD);

        float vCh0 = (float)s->ch0Latest * (1.2f / 8388608.0f);
        uiSetVratio(vCh0);

        (void)hwD;

        /* ── Phase 9b: Battery + charger + MCU temp (1 Hz) ────── */
        batteryPoll();
        uiSetBattery(batteryGetVoltage(), batteryGetSocPercent(),
                     batteryGetChargeStateStr());
        uiSetUsbStatus(batteryIsUsbConnected() ? "CONNECTED" : "---");
        uiSetMcuTemp(battGetMcuTempX10() / 10.0f);

        printf("BATT: %.2fV %s CHG:%s [%s] USB:%s MCU:%.1fC VDDA:%lumV VCORE:%lumV LED:%s\r\n",
               (double)batteryGetVoltage(),
               batteryGetSocStr(),
               batteryGetChargeStateStr(),
               batteryGetGpioDebugStr(),
               batteryIsUsbConnected() ? "YES" : "NO",
               (double)battGetMcuTempX10() / 10.0,
               (unsigned long)battGetVddaMv(),
               (unsigned long)battGetVddCoreMv(),
               ledStatusGetDiagStr());

        /* ── Phase 10: decimation rate diagnostic (1 Hz) ───────── */
        {
          static uint32_t prevAdcRec, prevForceRec;
          uint32_t curAdc   = dpGetAdcRecordCount();
          uint32_t curForce = dpGetForceRecordCount();
          printf("DECIM: ADC=%lu/s FORCE=%lu/s F=%.2fN miss=%lu\r\n",
                 (unsigned long)(curAdc - prevAdcRec),
                 (unsigned long)(curForce - prevForceRec),
                 (double)dpGetLatestForceN(),
                 (unsigned long)missD);
          prevAdcRec   = curAdc;
          prevForceRec = curForce;
        }
      }
    }

#ifdef VIZ_STREAM
    /* ── Visualizer streaming: 20 Hz $IMU CSV — CDC only, no UART ── */
    {
      static uint32_t lastViz;
      static uint32_t lastHb;
      uint32_t nowViz = HAL_GetTick();

      /* 20 Hz CDC stream */
      if (nowViz - lastViz >= 50)
      {
        lastViz = nowViz;
        imuData_t imu;
        if (imuRead(&imu) == 0)
        {
          static char vizBuf[256];
          int n = snprintf(vizBuf, sizeof(vizBuf),
                 "$IMU,%lu,%+.3f,%+.3f,%+.3f,%+.1f,%+.1f,%+.1f,"
                 "%+.5f,%+.5f,%+.5f,%+.5f,%+.2f,%+.2f,%+.2f,"
                 "%+.3f,%+.3f,%+.3f,%+.1f,%s\r\n",
                 (unsigned long)nowViz,
                 (double)imu.ax, (double)imu.ay, (double)imu.az,
                 (double)imu.gx, (double)imu.gy, (double)imu.gz,
                 (double)imu.qw, (double)imu.qx, (double)imu.qy, (double)imu.qz,
                 (double)imu.roll, (double)imu.pitch, (double)imu.yaw,
                 (double)imu.driftX, (double)imu.driftY, (double)imu.driftZ,
                 (double)imu.tempC, imuGetGravAxis());
          if (n > 0)
            cdcWrite((const uint8_t *)vizBuf, (uint32_t)n);
        }
      }

      /* 10 s UART health report */
      if (nowViz - lastHb >= 10000)
      {
        lastHb = nowViz;
        uint32_t se, sr, fo, fm, sc;
        imuGetDiag(&se, &sr, &fo, &fm, &sc);
        uint32_t rc, mx, av;
        imuGetTimingDiag(&rc, &mx, &av);
        printf("IMU: t=%lu fmax=%lu ovf=%lu err=%lu rec=%lu sflp=%lu read=%luus/%luus g=%s\r\n",
               (unsigned long)nowViz,
               (unsigned long)fm, (unsigned long)fo,
               (unsigned long)se, (unsigned long)sr, (unsigned long)sc,
               (unsigned long)mx, (unsigned long)av,
               imuGetGravAxis());
        printf("SYS: V=%.2f %s CHG:%s USB:%s MCU:%.1fC  LED:%s\r\n",
               (double)batteryGetVoltage(),
               batteryGetSocStr(),
               batteryGetChargeStateStr(),
               batteryIsUsbConnected() ? "YES" : "NO",
               (double)battGetMcuTempX10() / 10.0,
               ledStatusGetDiagStr());
      }
    }
#else
    /* ── Normal mode: IMU polling at ~10 Hz + 1 Hz print ─────── */
    {
      static uint32_t lastImuTick;
      static uint32_t lastImuPrint;
      uint32_t nowImu = HAL_GetTick();
      if (nowImu - lastImuTick >= 100)
      {
        lastImuTick = nowImu;
        imuData_t imu;
        if (imuRead(&imu) == 0)
        {
          uiSetAccel(imu.ax, imu.ay, imu.az);
          uiSetGyro(imu.gx, imu.gy, imu.gz);
          uiSetImuDrift(imu.driftX, imu.driftY, imu.driftZ);
          uiSetQuat(imu.qw, imu.qx, imu.qy, imu.qz);
          uiSetEuler(imu.roll, imu.pitch, imu.yaw);
          uiSetImuTemp(imu.tempC);

          if (nowImu - lastImuPrint >= 1000)
          {
            lastImuPrint = nowImu;
            printf("IMU: ax=%+.2f ay=%+.2f az=%+.2f gx=%+.1f gy=%+.1f gz=%+.1f T=%+.1fC\r\n",
                   (double)imu.ax, (double)imu.ay, (double)imu.az,
                   (double)imu.gx, (double)imu.gy, (double)imu.gz,
                   (double)imu.tempC);
            printf("     q=%+.4f,%+.4f,%+.4f,%+.4f rpy=%+.1f,%+.1f,%+.1f d=%+.2f,%+.2f,%+.2f\r\n",
                   (double)imu.qw, (double)imu.qx, (double)imu.qy, (double)imu.qz,
                   (double)imu.roll, (double)imu.pitch, (double)imu.yaw,
                   (double)imu.driftX, (double)imu.driftY, (double)imu.driftZ);
            printf("     BATT: %.2fV %s CHG:%s USB:%s MCU:%.1fC  LED:%s\r\n",
                   (double)batteryGetVoltage(),
                   batteryGetSocStr(),
                   batteryGetChargeStateStr(),
                   batteryIsUsbConnected() ? "YES" : "NO",
                   (double)battGetMcuTempX10() / 10.0,
                   ledStatusGetDiagStr());
          }
        }
      }
    }
#endif
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI
                              |RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 31;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 10;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 2048;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/* USER CODE BEGIN 4 */

/**
 * @brief  HAL EXTI rising callback — logStart button only (EXTI4).
 * @note   ADC DRDY uses the fast EXTI2 path; never handled here.
 */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == logStart_Pin)
    appStateButtonIsr();
}

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
  /* TIM3 overflow: 16-bit DRDY edge counter wraps at 64 kHz (~once/second).
   * @see diag_timers.c diagDrdyTim3Overflow() */
  if (htim->Instance == TIM3)
  {
    diagDrdyTim3Overflow();
  }
  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Non-fatal: return to caller so remaining peripherals can init.
   * This prevents MX_SDMMC1_SD_Init (no SD card) from bricking the
   * entire system.  Truly critical failures (clock, power) always
   * succeed on this hardware; non-critical ones (SD, SPI peripherals
   * with nothing connected) are handled by their callers. */
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
