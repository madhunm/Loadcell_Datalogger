---
name: IMU freeze diagnostic fix
overview: Instrument the fault handlers to identify the exact crash source, add a diagnostic HardFault handler that reports via UART, guard all floating-point paths against NaN/Inf, and add a proper HA01+SFLP runtime validation.
todos:
  - id: fault-handlers
    content: Instrument HardFault/UsageFault/BusFault/MemManage with UART dump of SCB registers
    status: completed
  - id: float-guards
    content: Bulletproof halfToFloat, W derivation, quatToEuler, swQuatIntegrate against NaN/Inf
    status: completed
  - id: ha01-sflp-test
    content: Replace register-read HA01 probe with actual FIFO sample validation
    status: completed
  - id: cdc-buffer
    content: Increase CDC_TX_BUF_SIZE from 2048 to 4096
    status: completed
isProject: false
---

# IMU HardFault Diagnostic and Fix

## Root Cause Analysis

The MCU enters a `while(1)` loop in one of the fault handlers ([stm32h5xx_it.c](Core/Src/stm32h5xx_it.c) lines 97-107). All four fault handlers (HardFault, MemManage, BusFault, UsageFault) are silent infinite loops -- we have zero visibility when a fault occurs.

The most likely trigger: when the board moves, the SFLP FIFO returns half-precision quaternion values that, after decoding via `halfToFloat()`, produce `qx^2 + qy^2 + qz^2 > 1.0`. The `sqrtf(1.0 - sum2)` on line 529 of [imu_lsm6dsv.c](Core/Src/imu_lsm6dsv.c) should be guarded, but it only catches `sum2 < 1.0f` -- if `sum2` is exactly 1.0 or NaN (from garbage half-float data), `sqrtf` receives 0.0 or NaN, which can propagate. More critically, `atan2f` and `asinf` in `quatToEuler` can return NaN, and `snprintf("%+.5f", NaN)` on some embedded libcs can take excessive time or fault.

### Answering your questions

**Are we testing HA01+SFLP properly?** No. The current test in `imuInit()` only checks if the `sflp_game_rotation_get` register reads back 1 after a 20 ms wait. This verifies the register stuck but does NOT verify that the SFLP engine actually produces valid quaternion data at HA01 2000 Hz. A proper test must read at least one valid SFLP FIFO sample.

**Are we using the library's quaternion, or calculating manually?** The ST BSP driver (`lsm6dsv_reg.h/.c`) has NO built-in quaternion decoder. The SFLP outputs are raw half-precision float16 bytes in the FIFO -- the library only provides `lsm6dsv_fifo_out_raw_get()` which gives us 6 bytes + a tag. We must decode the half-floats and derive W ourselves. Our `halfToFloat()` and W derivation from `sqrt(1 - x^2 - y^2 - z^2)` is the correct and only approach. ST's own example code does the same thing.

## Plan

### 1. Instrument fault handlers with UART dump

In [stm32h5xx_it.c](Core/Src/stm32h5xx_it.c), replace the silent `while(1)` in `HardFault_Handler` (and optionally the others) with a UART dump that prints:
- Which fault (HardFault / UsageFault / BusFault / MemManage)
- SCB->CFSR (Configurable Fault Status Register) -- tells us exactly what faulted
- SCB->HFSR (HardFault Status Register)
- SCB->BFAR, SCB->MMFAR (fault address registers)
- LR value (to identify the calling function)

This uses direct `HAL_UART_Transmit` (not printf, which could be the thing that faulted). Then enters the `while(1)` loop.

### 2. Bulletproof all float math in imuRead

In [imu_lsm6dsv.c](Core/Src/imu_lsm6dsv.c):

- **`halfToFloat()`**: After conversion, reject NaN/Inf values by clamping to 0.0f. Add: `if (r.f != r.f || r.f > 1e6f || r.f < -1e6f) return 0.0f;` (NaN fails the `!=` self-test).

- **W derivation**: Already guarded with `sum2 < 1.0f`, but also guard the normalisation `sqrtf` — if `qnorm` is NaN, skip normalisation.

- **`quatToEuler()`**: `asinf` can return NaN if the argument drifts outside [-1,1]. Clamp `sinp` to [-1, 1] before calling `asinf`.

- **`swQuatIntegrate()`**: The `1.0f / sqrtf(...)` can produce Inf if the norm is ~0. Guard against zero norm.

### 3. Proper HA01+SFLP runtime validation

Replace the current "register-read" probe in `imuInit()` with a proper test that waits for at least one valid SFLP FIFO sample at each HA01 ODR candidate:

```c
// After enabling SFLP + setting ODR, configure temp FIFO + batch
// Wait up to 200ms polling fifo_status for a game_rotation tag
// If found → ODR is valid
// If timeout → ODR+SFLP combo doesn't work, try next
```

This confirms SFLP actually produces data at the given HA01 rate, not just that the enable register sticks.

### 4. Add the CDC buffer overflow guard

The 2 KB CDC ring buffer in [debug_uart.c](Core/Src/debug_uart.c) silently drops bytes when full. At 20 Hz x ~200 bytes/line = 4 KB/s into a 2 KB buffer that drains at USB FS speed (64 byte chunks), this is tight. Increase `CDC_TX_BUF_SIZE` from 2048 to 4096 to prevent data loss that could cause the visualizer to see partial lines and fail to parse.

### Files to modify

- [Core/Src/stm32h5xx_it.c](Core/Src/stm32h5xx_it.c) -- fault handler instrumentation
- [Core/Src/imu_lsm6dsv.c](Core/Src/imu_lsm6dsv.c) -- float guards, proper HA01 test
- [Core/Src/debug_uart.c](Core/Src/debug_uart.c) -- CDC buffer size increase
