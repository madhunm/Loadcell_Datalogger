################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/adc.c \
../Core/Src/adc_ads131m02.c \
../Core/Src/app_state.c \
../Core/Src/battery_monitor.c \
../Core/Src/calibration.c \
../Core/Src/cordic.c \
../Core/Src/crc.c \
../Core/Src/custom_bus.c \
../Core/Src/data_processing.c \
../Core/Src/debug_uart.c \
../Core/Src/debug_ui.c \
../Core/Src/diag_timers.c \
../Core/Src/fmac.c \
../Core/Src/gpdma.c \
../Core/Src/gpio.c \
../Core/Src/icache.c \
../Core/Src/imu_lsm6dsv.c \
../Core/Src/led_status.c \
../Core/Src/main.c \
../Core/Src/neopixel.c \
../Core/Src/osc_ltc6903.c \
../Core/Src/rtc.c \
../Core/Src/sdmmc.c \
../Core/Src/spi.c \
../Core/Src/stm32h5xx_hal_msp.c \
../Core/Src/stm32h5xx_hal_timebase_tim.c \
../Core/Src/stm32h5xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32h5xx.c \
../Core/Src/tim.c \
../Core/Src/usart.c \
../Core/Src/usb.c 

OBJS += \
./Core/Src/adc.o \
./Core/Src/adc_ads131m02.o \
./Core/Src/app_state.o \
./Core/Src/battery_monitor.o \
./Core/Src/calibration.o \
./Core/Src/cordic.o \
./Core/Src/crc.o \
./Core/Src/custom_bus.o \
./Core/Src/data_processing.o \
./Core/Src/debug_uart.o \
./Core/Src/debug_ui.o \
./Core/Src/diag_timers.o \
./Core/Src/fmac.o \
./Core/Src/gpdma.o \
./Core/Src/gpio.o \
./Core/Src/icache.o \
./Core/Src/imu_lsm6dsv.o \
./Core/Src/led_status.o \
./Core/Src/main.o \
./Core/Src/neopixel.o \
./Core/Src/osc_ltc6903.o \
./Core/Src/rtc.o \
./Core/Src/sdmmc.o \
./Core/Src/spi.o \
./Core/Src/stm32h5xx_hal_msp.o \
./Core/Src/stm32h5xx_hal_timebase_tim.o \
./Core/Src/stm32h5xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32h5xx.o \
./Core/Src/tim.o \
./Core/Src/usart.o \
./Core/Src/usb.o 

C_DEPS += \
./Core/Src/adc.d \
./Core/Src/adc_ads131m02.d \
./Core/Src/app_state.d \
./Core/Src/battery_monitor.d \
./Core/Src/calibration.d \
./Core/Src/cordic.d \
./Core/Src/crc.d \
./Core/Src/custom_bus.d \
./Core/Src/data_processing.d \
./Core/Src/debug_uart.d \
./Core/Src/debug_ui.d \
./Core/Src/diag_timers.d \
./Core/Src/fmac.d \
./Core/Src/gpdma.d \
./Core/Src/gpio.d \
./Core/Src/icache.d \
./Core/Src/imu_lsm6dsv.d \
./Core/Src/led_status.d \
./Core/Src/main.d \
./Core/Src/neopixel.d \
./Core/Src/osc_ltc6903.d \
./Core/Src/rtc.d \
./Core/Src/sdmmc.d \
./Core/Src/spi.d \
./Core/Src/stm32h5xx_hal_msp.d \
./Core/Src/stm32h5xx_hal_timebase_tim.d \
./Core/Src/stm32h5xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32h5xx.d \
./Core/Src/tim.d \
./Core/Src/usart.d \
./Core/Src/usb.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32H562xx -DUX_INCLUDE_USER_DEFINE_FILE -c -I../Core/Inc -I../X-CUBE-MEMS1/Target -I../Drivers/STM32H5xx_HAL_Driver/Inc -I../Drivers/STM32H5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H5xx/Include -I../Drivers/CMSIS/Include -I../Drivers/BSP/Components/lsm6dsv -I../Middlewares/ST/STM32_MotionID_Library/Inc -I../Middlewares/ST/STM32_MotionGC_Library/Inc -I../Middlewares/ST/STM32_MotionFX_Library/Inc -I../Middlewares/ST/STM32_MotionAC_Library/Inc -I../USBX/App -I../USBX/Target -I../Middlewares/ST/usbx/common/core/inc -I../Middlewares/ST/usbx/ports/generic/inc -I../Middlewares/ST/usbx/common/usbx_stm32_device_controllers -I../Middlewares/ST/usbx/common/usbx_device_classes/inc -I../Middlewares/Third_Party/FatFs/src -I../FATFS/App -I../FATFS/Target -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@"  -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/adc.cyclo ./Core/Src/adc.d ./Core/Src/adc.o ./Core/Src/adc.su ./Core/Src/adc_ads131m02.cyclo ./Core/Src/adc_ads131m02.d ./Core/Src/adc_ads131m02.o ./Core/Src/adc_ads131m02.su ./Core/Src/app_state.cyclo ./Core/Src/app_state.d ./Core/Src/app_state.o ./Core/Src/app_state.su ./Core/Src/battery_monitor.cyclo ./Core/Src/battery_monitor.d ./Core/Src/battery_monitor.o ./Core/Src/battery_monitor.su ./Core/Src/calibration.cyclo ./Core/Src/calibration.d ./Core/Src/calibration.o ./Core/Src/calibration.su ./Core/Src/cordic.cyclo ./Core/Src/cordic.d ./Core/Src/cordic.o ./Core/Src/cordic.su ./Core/Src/crc.cyclo ./Core/Src/crc.d ./Core/Src/crc.o ./Core/Src/crc.su ./Core/Src/custom_bus.cyclo ./Core/Src/custom_bus.d ./Core/Src/custom_bus.o ./Core/Src/custom_bus.su ./Core/Src/data_processing.cyclo ./Core/Src/data_processing.d ./Core/Src/data_processing.o ./Core/Src/data_processing.su ./Core/Src/debug_uart.cyclo ./Core/Src/debug_uart.d ./Core/Src/debug_uart.o ./Core/Src/debug_uart.su ./Core/Src/debug_ui.cyclo ./Core/Src/debug_ui.d ./Core/Src/debug_ui.o ./Core/Src/debug_ui.su ./Core/Src/diag_timers.cyclo ./Core/Src/diag_timers.d ./Core/Src/diag_timers.o ./Core/Src/diag_timers.su ./Core/Src/fmac.cyclo ./Core/Src/fmac.d ./Core/Src/fmac.o ./Core/Src/fmac.su ./Core/Src/gpdma.cyclo ./Core/Src/gpdma.d ./Core/Src/gpdma.o ./Core/Src/gpdma.su ./Core/Src/gpio.cyclo ./Core/Src/gpio.d ./Core/Src/gpio.o ./Core/Src/gpio.su ./Core/Src/icache.cyclo ./Core/Src/icache.d ./Core/Src/icache.o ./Core/Src/icache.su ./Core/Src/imu_lsm6dsv.cyclo ./Core/Src/imu_lsm6dsv.d ./Core/Src/imu_lsm6dsv.o ./Core/Src/imu_lsm6dsv.su ./Core/Src/led_status.cyclo ./Core/Src/led_status.d ./Core/Src/led_status.o ./Core/Src/led_status.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/neopixel.cyclo ./Core/Src/neopixel.d ./Core/Src/neopixel.o ./Core/Src/neopixel.su ./Core/Src/osc_ltc6903.cyclo ./Core/Src/osc_ltc6903.d ./Core/Src/osc_ltc6903.o ./Core/Src/osc_ltc6903.su ./Core/Src/rtc.cyclo ./Core/Src/rtc.d ./Core/Src/rtc.o ./Core/Src/rtc.su ./Core/Src/sdmmc.cyclo ./Core/Src/sdmmc.d ./Core/Src/sdmmc.o ./Core/Src/sdmmc.su ./Core/Src/spi.cyclo ./Core/Src/spi.d ./Core/Src/spi.o ./Core/Src/spi.su ./Core/Src/stm32h5xx_hal_msp.cyclo ./Core/Src/stm32h5xx_hal_msp.d ./Core/Src/stm32h5xx_hal_msp.o ./Core/Src/stm32h5xx_hal_msp.su ./Core/Src/stm32h5xx_hal_timebase_tim.cyclo ./Core/Src/stm32h5xx_hal_timebase_tim.d ./Core/Src/stm32h5xx_hal_timebase_tim.o ./Core/Src/stm32h5xx_hal_timebase_tim.su ./Core/Src/stm32h5xx_it.cyclo ./Core/Src/stm32h5xx_it.d ./Core/Src/stm32h5xx_it.o ./Core/Src/stm32h5xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32h5xx.cyclo ./Core/Src/system_stm32h5xx.d ./Core/Src/system_stm32h5xx.o ./Core/Src/system_stm32h5xx.su ./Core/Src/tim.cyclo ./Core/Src/tim.d ./Core/Src/tim.o ./Core/Src/tim.su ./Core/Src/usart.cyclo ./Core/Src/usart.d ./Core/Src/usart.o ./Core/Src/usart.su ./Core/Src/usb.cyclo ./Core/Src/usb.d ./Core/Src/usb.o ./Core/Src/usb.su

.PHONY: clean-Core-2f-Src

