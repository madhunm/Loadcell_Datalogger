---
name: Close All Blockers Gotchas
overview: 8 of 9 blockers/gotchas are already closed from Phase 0 and Phase 1. The only remaining item is GOTCHA 6 (FatFS manual add), which requires creating project glue files and obtaining the FatFS source library.
todos:
  - id: fatfs-source
    content: Obtain FatFS R0.15 source (ff.c, ff.h, diskio.h, ffunicode.c) and place in Middlewares/Third_Party/FatFs/src/
    status: completed
  - id: fatfs-ffconf
    content: Create ffconf.h with project-specific settings (FF_USE_FASTSEEK=1, FF_USE_LFN=1, FF_FS_REENTRANT=0, etc.)
    status: completed
  - id: fatfs-diskio
    content: Create FATFS/Target/sd_diskio.c and sd_diskio.h bridging FatFS diskio to HAL_SD via hsd1
    status: completed
  - id: fatfs-app
    content: Create FATFS/App/fatfs.c and fatfs.h with MX_FATFS_Init() and f_mount()
    status: completed
  - id: fatfs-includes
    content: Add FatFS include paths to STM32CubeIDE build settings (manual IDE step)
    status: completed
isProject: false
---

# Close All Blockers / Gotchas

## Status Dashboard

| # | Item | Status | Where |
|---|------|--------|-------|
| BLOCKER 1 | Linker heap/stack | DONE | `STM32H562RGTX_FLASH.ld` line 42-43: `0x4000` / `0x2000` |
| BLOCKER 2 | CDC instance pointer | DONE | `ux_device_cdc_acm.c` stores `g_cdc_acm`, getter exported in `.h` |
| BLOCKER 3 | USBX pool size | DONE | `app_usbx.h` USER CODE EC: `#undef`/`#define` to 4096 |
| BLOCKER 4 | CDC write state machine | DONE | `debug_uart.c` ring buffer + `cdcPoll()` state machine |
| BLOCKER 5 | NVIC priorities | DONE | `main.c` USER CODE 2: 11 `HAL_NVIC_SetPriority()` calls |
| **GOTCHA 6** | **FatFS manual add** | **REMAINING** | **No `FATFS/` or `FatFs/` dirs exist yet** |
| GOTCHA 7 | `_ux_utility_time_get` returned 0 | DONE | `app_usbx.c`: `time_tick = (ULONG)HAL_GetTick()` |
| GOTCHA 8 | Init order / EXTI2 guard | DONE | `main.c` USER CODE 2: disable EXTI2 at top, re-enable after init |
| GOTCHA 9 | USB DCD registration | DONE | `main.c` USER CODE 2: `ux_dcd_stm32_initialize` + `HAL_PCD_Start` |

---

## GOTCHA 6: FatFS Manual Add (the only remaining item)

FatFS is not CubeMX-managed in this project. SDMMC1 HAL is configured (`sdmmc.c` exists), but there is no filesystem layer. We need to add Elm Chan FatFS and bridge it to HAL_SD.

### File Structure to Create

```
Middlewares/Third_Party/FatFs/src/
    ff.c              -- FatFS core (EXTERNAL: copy from source)
    ff.h              -- FatFS API header (EXTERNAL: copy from source)
    ffconf.h          -- Configuration (WE WRITE THIS)
    diskio.h          -- Disk I/O interface defs (EXTERNAL: copy from source)
    ffunicode.c       -- Unicode tables (EXTERNAL: copy from source)

FATFS/Target/
    sd_diskio.c       -- Bridge: FatFS diskio -> HAL_SD (WE WRITE THIS)
    sd_diskio.h       -- (WE WRITE THIS)

FATFS/App/
    fatfs.c           -- MX_FATFS_Init() + f_mount() (WE WRITE THIS)
    fatfs.h           -- (WE WRITE THIS)
```

### Step 1: Obtain FatFS Source (Manual Step)

FatFS R0.15 source files need to be obtained from one of:
- **Option A:** STM32CubeH5 firmware package (if installed): look under `Middlewares/Third_Party/FatFs/src/`
- **Option B:** Download from http://elm-chan.org/fsw/ff/00index_e.html

Required files to copy into `Middlewares/Third_Party/FatFs/src/`:
- `ff.c` (core)
- `ff.h` (API)
- `diskio.h` (interface definitions)
- `ffunicode.c` (Unicode/code page tables)

We do NOT copy `ffconf.h` from the download -- we write our own.

### Step 2: Create `ffconf.h` (Configuration)

**File:** `Middlewares/Third_Party/FatFs/src/ffconf.h`

Key settings per the master plan:

```c
#define FF_FS_TINY       0       /* Full buffering */
#define FF_USE_FASTSEEK  1       /* Pre-allocation for logging */
#define FF_USE_LFN       1       /* Long file names, heap alloc */
#define FF_LFN_UNICODE   0       /* OEM char set */
#define FF_FS_REENTRANT  0       /* No RTOS */
#define FF_FS_NORTC      0       /* Use RTC for timestamps */
#define FF_MIN_SS        512
#define FF_MAX_SS        512
#define FF_VOLUMES       1
#define FF_USE_MKFS      1       /* Allow formatting from firmware */
#define FF_CODE_PAGE     437     /* US English */
```

### Step 3: Create `sd_diskio.c` / `sd_diskio.h` (HAL Bridge)

**File:** `FATFS/Target/sd_diskio.c`

Bridges FatFS `disk_read`/`disk_write`/`disk_ioctl` to `HAL_SD_ReadBlocks`/`HAL_SD_WriteBlocks` using the existing `hsd1` handle from `sdmmc.c`. Uses polling (not DMA) -- 1 ms per 4 KB write is fine for 16 KB/s logging rate.

The full implementation is specified in the master plan (lines 1035-1110 of `snazzy-petting-mountain.md`), including `get_fattime()` which returns a fixed date for now (RTC integration comes later).

### Step 4: Create `fatfs.c` / `fatfs.h` (App Layer)

**File:** `FATFS/App/fatfs.c`

Simple init: declares `FATFS` object, calls `f_mount()`. `MX_FATFS_Init()` will be called from `main.c` USER CODE BEGIN 2 when Phase 4 is reached.

### Step 5: Add Include Paths (Manual IDE Step)

In STM32CubeIDE: Project Properties > C/C++ Build > Settings > MCU GCC Compiler > Include paths, add:
- `../Middlewares/Third_Party/FatFs/src`
- `../FATFS/App`
- `../FATFS/Target`

### CubeMX Safety

All files are new and outside CubeMX's control -- inherently safe. No CubeMX-generated files are modified for this gotcha.

### Important Note

GOTCHA 6 is functionally needed at **Phase 4** (FatFS SD Card), not Phase 1. The master plan lists it in the "Pre-Code Blockers" section because the plan was written before Phase 1 was implemented. Blockers 1-5 and Gotchas 7-9 were the actual Phase 1 prerequisites, and they are all done. GOTCHA 6 can be implemented now to get it out of the way, or deferred until Phase 4 begins. Either approach is valid.

## Naming Convention Compliance

This plan has been retroactively updated so references to project application code use **camelCase** function names per [`.cursor/rules/commenting-and-naming.mdc`](../rules/commenting-and-naming.mdc). HAL-, CubeMX-, FatFS-, and USBX-required identifiers (for example `f_mount()`, `disk_read`, `_ux_utility_time_get`, `HAL_SD_ReadBlocks`) are left unchanged.
