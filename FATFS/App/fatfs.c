/**
  ******************************************************************************
  * @file    fatfs.c
  * @brief   FatFS application layer — init and mount
  *
  *          Declares the FATFS filesystem object and mounts volume "0:".
  *          Called from main.c during init (Phase 4+).
  ******************************************************************************
  */
#include "fatfs.h"
#include <stdio.h>

FATFS SDFatFS;
char  SDPath[4] = "0:/";

void MX_FATFS_Init(void)
{
    FRESULT res = f_mount(&SDFatFS, SDPath, 1);  /* 1 = mount now */
    if (res != FR_OK) {
        printf("[FATFS] f_mount FAILED: %d\r\n", (int)res);
    } else {
        printf("[FATFS] SD mounted OK\r\n");
    }
}
