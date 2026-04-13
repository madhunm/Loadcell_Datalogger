/**
 ******************************************************************************
 * @file    fatfs.c
 * @brief   FatFS application layer — dual logical volumes on one SD card.
 * @details Declares FATFS objects for "0:" (LOGGER) and "1:" (SYSCAL), maps
 *          volumes to MBR partitions via VolToPart[], and provides sdMountAll()
 *          as the sole mount entry point. MX_FATFS_Init() initialises objects
 *          only (no mount), per Phase 10b.
 ******************************************************************************
 */
#include "fatfs.h"
#include <stdio.h>

FATFS SDFatFS;
char  SDPath[4] = "0:/";

FATFS SysCalFatFS;
char  SysCalPath[] = "1:/";

PARTITION VolToPart[FF_VOLUMES] = {
    {0, 1},   /* "0:" → physical drive 0, partition 1 (LOGGER) */
    {0, 2},   /* "1:" → physical drive 0, partition 2 (SYSCAL) */
};

void MX_FATFS_Init(void)
{
    /* No f_mount here — sdMountAll() is the only mount path after SDMMC ready. */
}

/**
 * @brief  Mount both logical volumes on the SD card.
 * @return FR_OK if both mounted; otherwise an error code. If "1:" fails after
 *         "0:" succeeded, "0:" is unmounted so callers may safely run f_fdisk.
 */
FRESULT sdMountAll(void)
{
    FRESULT fr0 = f_mount(&SDFatFS, "0:", 1);
    if (fr0 != FR_OK)
        return fr0;

    FRESULT fr1 = f_mount(&SysCalFatFS, "1:", 1);
    if (fr1 != FR_OK)
    {
        f_unmount("0:");
        return fr1;
    }
    return FR_OK;
}
