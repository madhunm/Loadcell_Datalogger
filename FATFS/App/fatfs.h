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

extern FATFS SDFatFS;   /* filesystem object for volume "0:" */
extern char  SDPath[];  /* logical drive path */

void MX_FATFS_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* FATFS_H */
