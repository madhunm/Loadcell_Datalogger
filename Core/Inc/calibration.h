/**
 * @file    calibration.h
 * @brief   Load and access calibration from binary .cal files on SYSCAL volume.
 * @details Scans "1:*.cal" at boot, exposes cell list to the VT220 UI, loads a
 *          selected packed calFile_t with CRC16 validation, and provides a
 *          read-only calConfig_t for dpInit() and logging headers.
 * @author  Madhu
 * @date    2026-04-13
 */

#ifndef CALIBRATION_H
#define CALIBRATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CAL_FILE_MAGIC  0x43414C31UL  /* 'CAL1' */
#define CAL_FILE_VER    1

#define CAL_MAX_CELLS   8

/**
 * @brief  On-disk packed calibration blob (64 bytes, little-endian).
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;               /**< offset  0 */
    uint32_t serialNumber;        /**< offset  4 */
    uint16_t version;             /**< offset  8 */
    uint8_t  pad[2];              /**< offset 10 — alignment padding */
    float    sensitivityUvPerN;   /**< offset 12 */
    float    adcGainCh1;          /**< offset 16 */
    float    adcGainCh2;          /**< offset 20 */
    float    offsetCh1;           /**< offset 24 */
    float    offsetCh2;           /**< offset 28 */
    float    tareOffsetN;         /**< offset 32 — always 0 in file */
    float    battDividerRatio;    /**< offset 36 */
    float    preallocMb;          /**< offset 40 */
    float    enableAdcCrc;        /**< offset 44 */
    float    allowLogOnUsb;       /**< offset 48 */
    float    cellCorrFactor;      /**< offset 52 */
    uint8_t  reserved[6];         /**< offset 56 */
    uint16_t crc16;               /**< offset 62 — CRC of bytes 0..61 */
} calFile_t;

/**
 * @brief  One discovered factory calibration file on "1:".
 */
typedef struct {
    uint32_t serial;
    char     filename[16];
} calEntry_t;

/**
 * @brief  All calibration and logging configuration parameters.
 */
typedef struct {
    float    sensitivityUvPerN;   /**< Loadcell sensitivity (µV/N) */
    float    adcGainCh1;          /**< ADS131M02 CH0 PGA gain (1…128, float) */
    float    adcGainCh2;          /**< ADS131M02 CH1 PGA gain (typically 1) */
    int32_t  offsetCh1;           /**< ADC offset CH1 (raw counts) */
    int32_t  offsetCh2;           /**< ADC offset CH2 (raw counts) */
    float    tareOffsetN;         /**< Tare zero-offset (N), runtime */
    float    battDividerRatio;    /**< Battery voltage divider ratio */
    float    preallocMb;          /**< SD pre-allocation size (MB) */
    uint8_t  enableAdcCrc;        /**< 1 = compute CRC16 on ADC records */
    uint8_t  allowLogOnUsb;       /**< 1 = permit logging while USB active */
    float    cellCorrFactor;      /**< Per-cell sensitivity correction (~1.0) */
} calConfig_t;

/** @brief  Where the active calibration was loaded from. */
typedef enum {
    CAL_SRC_DEFAULT,   /**< Hardcoded defaults (no valid .cal) */
    CAL_SRC_SD_FILE,   /**< Loaded from binary .cal on SYSCAL volume */
    CAL_SRC_FLASH,     /**< Loaded from on-chip flash (future) */
} calSource_t;

/**
 * @brief  Legacy entry point — boot uses calScanFiles / calibrationLoadFromCal.
 * @return Always 0 (orchestration is in main.c).
 */
int calibrationLoad(void);

/**
 * @brief  Scan "1:" for *.cal files (after mount).
 */
void calScanFiles(void);

/**
 * @brief  Read-only access to scan results from calScanFiles().
 * @param[out] entries  Receives pointer to internal array (or NULL if @p entries NULL).
 * @param[out] count    Number of valid entries (≤ CAL_MAX_CELLS).
 */
void calibrationGetEntries(const calEntry_t **entries, uint8_t *count);

/**
 * @brief  Load and validate a .cal file for the given cell serial.
 * @param[in] serialNumber  Serial from filename / UI; 0 is invalid.
 * @return 0 on success, negative on error.
 */
int calibrationLoadFromCal(uint32_t serialNumber);

/**
 * @brief  Get the source that provided the active calibration.
 */
calSource_t calibrationGetSource(void);

/**
 * @brief  Get a read-only pointer to the active calibration config.
 */
const calConfig_t *calibrationGet(void);

/**
 * @brief  Factory cell serial for the active calibration (0 = none).
 */
uint32_t calibrationGetSerial(void);

/**
 * @brief  Update the tare zero-offset at runtime.
 * @param[in] tareN  New tare offset in Newtons.
 */
void calibrationSetTare(float tareN);

#ifdef __cplusplus
}
#endif

#endif /* CALIBRATION_H */
