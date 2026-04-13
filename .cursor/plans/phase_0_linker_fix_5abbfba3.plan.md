---
name: Phase 0 Linker Fix
overview: Complete Phase 0 by modifying the linker script heap and stack sizes to support printf float formatting, FatFS, and USB call chains in later phases.
todos:
  - id: linker-heap-stack
    content: Change _Min_Heap_Size to 0x4000 and _Min_Stack_Size to 0x2000 in STM32H562RGTX_FLASH.ld
    status: completed
isProject: false
---

# Phase 0 -- Linker Script Heap/Stack Fix

## What needs to change

One file, two lines. This is BLOCKER 1 from the plan.

**File:** [STM32H562RGTX_FLASH.ld](STM32H562RGTX_FLASH.ld) (lines 42-43)

**Current values:**
```ld
_Min_Heap_Size = 0x200;  /* 512 bytes */
_Min_Stack_Size = 0x1000; /* 4 KB */
```

**New values:**
```ld
_Min_Heap_Size = 0x4000;  /* 16 KB -- printf %f + FatFS needs this */
_Min_Stack_Size = 0x2000; /* 8 KB -- FatFS + printf + USB call depth */
```

## Why

- **16 KB heap:** newlib-nano `_dtoa_r()` allocates ~4 KB on first float printf. FatFS `f_mount()` allocates ~1 KB. USB descriptors use additional heap. 16 KB provides margin.
- **8 KB stack:** Worst-case call depth: `main()` -> `f_write()` -> `disk_write()` -> `HAL_SD_WriteBlocks()` + local buffers = ~3-4 KB. Add printf VT220 formatting on top = ~5-6 KB. 8 KB provides margin.

## CubeMX safety

The plan states "No more CubeMX regens planned," so direct edit of the linker script is safe. If a regen IS done later, these two lines must be restored manually (CubeMX resets them to defaults).

## RAM budget check

With 640 KB total RAM:
- Heap: 16 KB (was 0.5 KB)
- Stack: 8 KB (was 4 KB)
- Net increase: 19.5 KB
- Remaining for .data/.bss: ~616 KB -- more than enough

## Verification after build

- Check `.map` file to confirm heap/stack placement and sizes
- At runtime (Phase 1): fill stack with 0xDEADBEEF canary at boot, periodically check high-water mark
