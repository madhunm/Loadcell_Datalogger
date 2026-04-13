/**
  ******************************************************************************
  * @file    fatfs.h
  * @brief   FatFS application layer — header
  ******************************************************************************
  */
#ifndef FATFS_H
#define FATFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ff.h"

extern FATFS SDFatFS;      /**< Filesystem object for volume "0:" (LOGGER). */
extern char  SDPath[];

extern FATFS SysCalFatFS;  /**< Filesystem object for volume "1:" (SYSCAL). */
extern char  SysCalPath[];

#if FF_MULTI_PARTITION
extern PARTITION VolToPart[];
#endif

void MX_FATFS_Init(void);

/** @brief Mount "0:" and "1:"; unmounts "0:" if "1:" fails (for f_fdisk safety). */
FRESULT sdMountAll(void);

#ifdef __cplusplus
}
#endif

#endif /* FATFS_H */
