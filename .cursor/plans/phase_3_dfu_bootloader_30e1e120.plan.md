---
name: Phase 3 DFU Bootloader
overview: "Add USB DFU bootloader jump: when userButton (PC13) is held at reset, the MCU jumps to the ROM bootloader for firmware upload via STM32CubeProgrammer over USB -- no STLink needed."
todos:
  - id: dfu-jump
    content: Add DFU bootloader jump code at the top of main.c USER CODE BEGIN 2, before all peripheral init
    status: cancelled
  - id: dfu-test
    content: "Test: hold userButton at reset, verify STM32CubeProgrammer detects DFU device; verify normal boot still works"
    status: cancelled
isProject: false
---

# Phase 3 -- USB DFU Bootloader

## What This Phase Delivers

A one-time check at the very start of `main()`: if `userButton` (PC13) is held low at boot, the MCU disables all interrupts and jumps to the STM32H562 ROM bootloader at system memory. STM32CubeProgrammer can then flash new firmware over USB DFU. Normal boot (button not held) proceeds as usual.

## Scope

This is a single code insertion in [Core/Src/main.c](Core/Src/main.c). No new files, no changes to headers, no linker script changes.

## Implementation

### Where: `main.c` USER CODE BEGIN 2, **before** all other init code

The DFU check must be the **first** thing in USER CODE BEGIN 2, before `MX_USB_PCD_Init()`, before USBX init, before the panel draw. If the button is held, we jump immediately -- no peripherals initialized, no state to clean up.

### Code (from master plan):

```c
/* DFU bootloader jump — hold userButton (PC13) at reset */
if (HAL_GPIO_ReadPin(userButton_GPIO_Port, userButton_Pin) == GPIO_PIN_RESET)
{
    __HAL_RCC_USB_CLK_ENABLE();
    HAL_Delay(100);
    SysTick->CTRL = 0;
    __disable_irq();
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    const uint32_t BOOTLOADER_ADDR = 0x0BF97000UL;
    __set_MSP(*(uint32_t *)BOOTLOADER_ADDR);
    ((void (*)(void))(*(uint32_t *)(BOOTLOADER_ADDR + 4)))();
}
```

### Key details

- **`GPIO_PIN_RESET`**: userButton is configured as input, no pull (PC13). The button connects to GND when pressed, so held = LOW = RESET.
- **`__HAL_RCC_USB_CLK_ENABLE()`**: The ROM bootloader needs the USB clock running to enumerate as DFU.
- **`HAL_Delay(100)`**: Brief settle time after clock enable.
- **Interrupt teardown**: SysTick disabled, all IRQs disabled and pending cleared -- clean slate for bootloader.
- **`0x0BF97000UL`**: STM32H562 system memory base per RM0481. The bootloader's vector table starts here: first word is initial MSP, second word is reset handler address.
- **CubeMX safe**: Entirely within `/* USER CODE BEGIN 2 */` ... `/* USER CODE END 2 */`.

### Bootloader address verification

The address `0x0BF97000` is for the STM32H562 family. This should be verified against RM0481 "System memory" in the memory map table. If the board HardFaults on jump, this is the first thing to check.

## Testing Procedure

1. Flash the updated firmware via STLink (normal method)
2. Disconnect STLink if desired
3. Hold `userButton`, press the reset button (or power cycle)
4. Open STM32CubeProgrammer, select "USB" connection, click "Connect"
5. CubeProgrammer should detect a DFU device
6. Select a `.bin` or `.elf` file and program it
7. Release button, press reset -- new firmware runs
8. Normal boot (button NOT held) -- application starts as before

## Success Criteria (from master plan)

- STM32CubeProgrammer detects device as DFU when userButton held at reset
- Can flash a `.elf` or `.bin` file successfully via USB
- Device boots into new firmware after DFU programming completes
- Normal boot (button not held) runs application as normal
