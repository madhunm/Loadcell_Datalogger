/**
 * @file    calibration.h
 * @brief   Load and access calibration / logging options from SD or defaults.
 * @details Parses config.txt (FatFS) at boot, tracks whether values came from
 *          SD, flash, or hardcoded defaults, and exposes a read-only calConfig_t
 *          consumed by dpInit() and the UI.
 * @author  Madhu
 * @date    2026-04-12
 */

#ifndef CALIBRATION_H
#define CALIBRATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  All calibration and logging configuration parameters.
 * @note   Members use project camelCase naming; on-disk config.txt keys use
 *         the same names (case-insensitive match).
 */
typedef struct {
    float    sensitivityUvPerN;   /**< Loadcell sensitivity (uV/N)          */
    uint8_t  adcGainCh1;          /**< ADS131M02 PGA gain CH1               */
    uint8_t  adcGainCh2;          /**< ADS131M02 PGA gain CH2               */
    int32_t  offsetCh1;           /**< ADC offset CH1 (raw counts)          */
    int32_t  offsetCh2;           /**< ADC offset CH2 (raw counts)          */
    float    tareOffsetN;         /**< Tare zero-offset (N)                 */
    float    battDividerRatio;    /**< Battery voltage divider ratio        */
    uint32_t preallocMb;          /**< SD pre-allocation size (MB)          */
    uint8_t  enableAdcCrc;        /**< 1 = compute CRC16 on ADC records     */
    uint8_t  allowLogOnUsb;       /**< 1 = permit logging while USB active  */
} calConfig_t;

/** @brief  Where the active calibration was loaded from. */
typedef enum {
    CAL_SRC_DEFAULT,   /**< Hardcoded defaults (no SD file, no flash) */
    CAL_SRC_SD_FILE,   /**< Parsed from config.txt on SD card         */
    CAL_SRC_FLASH,     /**< Loaded from on-chip flash (Phase 13)      */
} calSource_t;

/**
 * @brief  Load calibration: try SD config.txt, then flash, then defaults.
 * @return Number of keys successfully parsed from the source (0 = defaults).
 * @pre    FatFS volume "0:" must be mounted.
 * @post   calibrationGet() returns the active config.
 */
int calibrationLoad(void);

/**
 * @brief  Get the source that provided the active calibration.
 * @return CAL_SRC_DEFAULT, CAL_SRC_SD_FILE, or CAL_SRC_FLASH.
 */
calSource_t calibrationGetSource(void);

/**
 * @brief  Get a read-only pointer to the active calibration config.
 * @return Pointer to the module-static calConfig_t.
 */
const calConfig_t *calibrationGet(void);

/**
 * @brief  Update the tare zero-offset at runtime.
 * @param[in] tareN  New tare offset in Newtons.
 */
void calibrationSetTare(float tareN);

#ifdef __cplusplus
}
#endif

#endif /* CALIBRATION_H */
