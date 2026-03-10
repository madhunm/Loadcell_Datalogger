#pragma once
#include <cstdint>
#include "services/led_ui.h"

enum class FaultCode : uint32_t {
  SD_MOUNT_FAIL = 1u << 0,
  SD_WRITE_FAIL = 1u << 1,
  ADC_FAULT     = 1u << 2,
  IMU_FAULT     = 1u << 3,
};

enum class WarningCode : uint32_t {
  RTC_INVALID = 1u << 0,
  RTC_FAULT   = 1u << 1,
  LOW_BATT    = 1u << 2,
  UNDERLOAD   = 1u << 3,
  OVERLOAD    = 1u << 4,
  COMPRESSION = 1u << 5,
  IMU_WARN    = 1u << 6,
  BATT_WARN   = 1u << 7,
};

void system_status_set_fault(FaultCode c);
void system_status_clear_fault(FaultCode c);
void system_status_set_warning(WarningCode c);
void system_status_clear_warning(WarningCode c);

LedFault system_status_get_led_fault();
LedWarning system_status_get_led_warning();
