/**
 * @file    sdmmc_fatfs.h
 * @brief   Dual-volume FatFS logging session — binary on 1: (SYSCAL), CSV on 0: (LOGGER).
 * @details Opens paired files, pre-allocates space from cal, writes 64 B bin header,
 *          streams chunks with stall timing and periodic f_sync, truncates on close,
 *          emits UART qualification report.
 * @author  Madhu
 * @date    2026-04-13
 */

#ifndef SDMMC_FATFS_H
#define SDMMC_FATFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ff.h"
#include <stdint.h>

/**
 * @brief Runtime state for one logging session (both files).
 */
typedef struct {
    FIL      binFile;
    FIL      csvFile;
    uint8_t  isOpen;
    char     binFilename[32];
    char     csvFilename[32];
    uint32_t binBytesWritten;
    uint32_t csvBytesWritten;
    uint32_t sessionStartTick;
    uint32_t lastSyncTick;
    uint32_t adcPushBase;
    uint32_t forcePushBase;
    uint32_t missCountBase;
    uint32_t overflowBase;
    uint32_t adcCount;
    uint32_t forceCount;
    uint32_t metaCount;
    uint32_t csvCount;
    uint32_t stallMaxMs;
    uint32_t stallCount;
    uint32_t ringPeakUsed;
    uint32_t pressureEvents;
    uint32_t usbKeepaliveCount;
    uint32_t syncCount;
} sdSession_t;

/**
 * @brief  Create files, pre-allocate, write binary header and CSV preamble; print LOG START.
 * @param[in,out] s    Session struct (zeroed by caller before first open).
 * @return FR_OK on success, else FatFS error.
 * @pre    Both volumes mounted; calibration valid.
 */
FRESULT sdSessionOpen(sdSession_t *s);

/**
 * @brief  Write one binary chunk (typically from ring drain).
 * @param[in,out] s     Open session.
 * @param[in]     data  Payload bytes.
 * @param[in]     len   Length.
 * @return FR_OK on success.
 */
FRESULT sdSessionWriteBinChunk(sdSession_t *s, const void *data, UINT len);

/**
 * @brief  Write one CSV chunk.
 * @param[in,out] s     Open session.
 * @param[in]     data  Payload bytes.
 * @param[in]     len   Length.
 * @return FR_OK on success.
 */
FRESULT sdSessionWriteCsvChunk(sdSession_t *s, const void *data, UINT len);

/**
 * @brief  If 10 s elapsed since last sync, f_sync both files and bump syncCount.
 * @param[in,out] s  Open session.
 */
void sdSessionTrySync(sdSession_t *s);

/**
 * @brief  Truncate to written lengths, close files, print LOG STOP and PASS/FAIL.
 * @param[in,out] s  Session to close.
 * @return FR_OK on success.
 */
FRESULT sdSessionClose(sdSession_t *s);

#ifdef __cplusplus
}
#endif

#endif /* SDMMC_FATFS_H */
