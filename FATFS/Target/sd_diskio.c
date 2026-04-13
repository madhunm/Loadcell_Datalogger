/**
  ******************************************************************************
  * @file    sd_diskio.c
  * @brief   FatFS diskio bridge to HAL_SD (DMA / IDMA mode)
  *
  *          Uses HAL_SD_ReadBlocks_DMA / HAL_SD_WriteBlocks_DMA because the
  *          STM32H5 SDMMC peripheral requires IDMA for data transfers.
  *          Polling-mode HAL_SD_ReadBlocks fails with DATA_TIMEOUT on H5.
  ******************************************************************************
  */
#include "ff.h"
#include "diskio.h"
#include "sdmmc.h"
#include "stm32h5xx_hal.h"
#include <string.h>

#define SD_TIMEOUT_MS  5000

static volatile uint8_t sd_rx_complete;
static volatile uint8_t sd_tx_complete;

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    sd_rx_complete = 1;
}

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    sd_tx_complete = 1;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER)
        return 0;
    return STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER)
        return 0;
    return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0) return RES_PARERR;

    /* IDMA requires 4-byte aligned buffer */
    static uint8_t scratch[512] __attribute__((aligned(4)));
    uint8_t *dst = buff;
    int use_scratch = ((uint32_t)buff & 3U) != 0;

    for (UINT i = 0; i < count; i++)
    {
        uint8_t *target = use_scratch ? scratch : (dst + i * 512U);

        sd_rx_complete = 0;
        if (HAL_SD_ReadBlocks_DMA(&hsd1, target, sector + i, 1) != HAL_OK)
            return RES_ERROR;

        uint32_t t0 = HAL_GetTick();
        while (!sd_rx_complete)
        {
            if (HAL_GetTick() - t0 > SD_TIMEOUT_MS)
                return RES_ERROR;
        }
        while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
        {
            if (HAL_GetTick() - t0 > SD_TIMEOUT_MS)
                return RES_ERROR;
        }

        if (use_scratch)
            memcpy(dst + i * 512U, scratch, 512);
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0) return RES_PARERR;

    static uint8_t scratch[512] __attribute__((aligned(4)));
    const uint8_t *src = (const uint8_t *)buff;
    int use_scratch = ((uint32_t)buff & 3U) != 0;

    for (UINT i = 0; i < count; i++)
    {
        uint8_t *target;
        if (use_scratch)
        {
            memcpy(scratch, src + i * 512U, 512);
            target = scratch;
        }
        else
        {
            target = (uint8_t *)(src + i * 512U);
        }

        sd_tx_complete = 0;
        if (HAL_SD_WriteBlocks_DMA(&hsd1, target, sector + i, 1) != HAL_OK)
            return RES_ERROR;

        uint32_t t0 = HAL_GetTick();
        while (!sd_tx_complete)
        {
            if (HAL_GetTick() - t0 > SD_TIMEOUT_MS)
                return RES_ERROR;
        }
        while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
        {
            if (HAL_GetTick() - t0 > SD_TIMEOUT_MS)
                return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0) return RES_PARERR;

    HAL_SD_CardInfoTypeDef info;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        HAL_SD_GetCardInfo(&hsd1, &info);
        *(DWORD *)buff = info.LogBlockNbr;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        HAL_SD_GetCardInfo(&hsd1, &info);
        *(DWORD *)buff = info.LogBlockSize / 512;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

DWORD get_fattime(void)
{
    /* TODO: read RTC and return packed FAT timestamp */
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) |
           ((DWORD)1 << 16) | (0 << 11) | (0 << 5) | (0 >> 1);
}
