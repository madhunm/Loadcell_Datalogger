---
name: Phase 4 FatFS SD
overview: Enable FatFS SD card mount, write a TEST.TXT file, report card detect and free space on the VT220 UI and UART debug stream. All infrastructure files already exist; the work is wiring them into main.c and integrating with the debug UI.
todos:
  - id: enable-sdmmc-init
    content: "Uncomment MX_SDMMC1_SD_Init() in main.c and add #include \"fatfs.h\""
    status: completed
  - id: fatfs-init-test
    content: Add MX_FATFS_Init() + TEST.TXT write + free space report + SD status to UI in USER CODE BEGIN 2
    status: completed
  - id: nvic-sdmmc
    content: Add HAL_NVIC_SetPriority(SDMMC1_IRQn, 5, 0) in USER CODE BEGIN 2
    status: completed
  - id: flash-test
    content: "User test: boot with SD card inserted, verify TEST.TXT on PC; boot without card, verify 'card not detected' message"
    status: completed
isProject: false
---

# Phase 4 -- FatFS SD Card

## Current State

All FatFS infrastructure already exists in the project:
- **FatFS core**: [Middlewares/Third_Party/FatFs/src/ff.c](Middlewares/Third_Party/FatFs/src/ff.c), [ff.h](Middlewares/Third_Party/FatFs/src/ff.h), [ffunicode.c](Middlewares/Third_Party/FatFs/src/ffunicode.c), [diskio.h](Middlewares/Third_Party/FatFs/src/diskio.h)
- **Config**: [Middlewares/Third_Party/FatFs/src/ffconf.h](Middlewares/Third_Party/FatFs/src/ffconf.h) -- already configured per master plan (FF_USE_FASTSEEK=1, FF_USE_LFN=1, FF_FS_REENTRANT=0, etc.)
- **diskio bridge**: [FATFS/Target/sd_diskio.c](FATFS/Target/sd_diskio.c) -- polling HAL_SD read/write via `hsd1`, `get_fattime()` returns fixed 2026-01-01
- **App layer**: [FATFS/App/fatfs.c](FATFS/App/fatfs.c) -- `MX_FATFS_Init()` does `f_mount()` and prints status
- **Include paths**: Already in `.cproject` for both Debug and Release (`../Middlewares/Third_Party/FatFs/src`, `../FATFS/App`, `../FATFS/Target`)
- **SDMMC HAL**: [Core/Src/sdmmc.c](Core/Src/sdmmc.c) -- `MX_SDMMC1_SD_Init()` with card detect guard (PA8 active-low: LOW=card present, HIGH=no card, skips init)
- **Card detect pin**: PA8 as `SDMMC1_Card_Detect_Pin` / `SDMMC1_Card_Detect_GPIO_Port` (defined in `main.h`)
- **NVIC**: `SDMMC1_IRQn` already has IRQ handler in `stm32h5xx_it.c`

## What Needs to Change

### 1. Enable `MX_SDMMC1_SD_Init()` in main.c

Currently commented out at line 106:
```c
/* MX_SDMMC1_SD_Init(); */    /* DISABLED — not needed for USB CDC */
```
Uncomment it so the SDMMC HAL initializes before FatFS tries to mount.

### 2. Add `MX_FATFS_Init()` + SD test write in USER CODE BEGIN 2

After the 5-second USB enumeration wait and before `uiDrawPanel()`, add:
- Call `MX_FATFS_Init()` (mount volume "0:")
- If mount succeeds, read free space with `f_getfree()` and report to debug
- Write `TEST.TXT` with a known string, close it, report success/failure
- Update the VT220 UI SD status field via `uiSetSdStatus()`
- Report card-not-detected if PA8 is HIGH

### 3. Add `#include "fatfs.h"` to main.c includes

In `USER CODE BEGIN Includes`, add the FatFS app header so `MX_FATFS_Init()` and `SDFatFS` are visible.

### 4. Add NVIC priority override for SDMMC1

There is currently **no** `HAL_NVIC_SetPriority(SDMMC1_IRQn, ...)` in main.c USER CODE BEGIN 2. The master plan specifies priority 5. We need to add this line so SDMMC doesn't compete with the ADC hot path (even though ADC isn't active yet in Phase 4, this sets up the correct priority for later phases).

## Files Modified

- [Core/Src/main.c](Core/Src/main.c) -- uncomment `MX_SDMMC1_SD_Init()`, add `#include "fatfs.h"`, add FatFS init + test write + SD status reporting, add SDMMC1 NVIC priority

No new files created. No header changes. No linker script changes.

## Success Criteria (from master plan)

- `f_mount()` returns `FR_OK`
- `f_open("0:TEST.TXT", FA_CREATE_ALWAYS|FA_WRITE)` returns `FR_OK`
- `f_write()` writes "Hello FatFS\r\n", `f_close()` returns `FR_OK`
- SD card inserted into PC shows `TEST.TXT` with correct content
- Card detect (PA8) correctly reflects card presence
- Serial terminal shows: `SD: mounted OK, free=XXXX MB` on boot
- Serial terminal shows: `SD: card not detected` if no card inserted

## Naming Convention Compliance

This plan has been retroactively updated to use `camelCase` naming for application functions per [`.cursor/rules/commenting-and-naming.mdc`](../rules/commenting-and-naming.mdc). HAL/CubeMX/FatFS identifiers (`MX_SDMMC1_SD_Init`, `MX_FATFS_Init`, `f_mount`, `f_open`, `f_write`, `f_close`, `f_getfree`) are excluded. When Phase 14 executes, these names serve as the target reference.
