#include "services/system_status.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static uint32_t s_fault_mask = 0;
static uint32_t s_warning_mask = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void system_status_set_fault(FaultCode c) {
  portENTER_CRITICAL(&s_mux);
  s_fault_mask |= (uint32_t)c;
  portEXIT_CRITICAL(&s_mux);
}

void system_status_clear_fault(FaultCode c) {
  portENTER_CRITICAL(&s_mux);
  s_fault_mask &= ~(uint32_t)c;
  portEXIT_CRITICAL(&s_mux);
}

void system_status_set_warning(WarningCode c) {
  portENTER_CRITICAL(&s_mux);
  s_warning_mask |= (uint32_t)c;
  portEXIT_CRITICAL(&s_mux);
}

void system_status_clear_warning(WarningCode c) {
  portENTER_CRITICAL(&s_mux);
  s_warning_mask &= ~(uint32_t)c;
  portEXIT_CRITICAL(&s_mux);
}

LedFault system_status_get_led_fault() {
  portENTER_CRITICAL(&s_mux);
  uint32_t m = s_fault_mask;
  portEXIT_CRITICAL(&s_mux);
  if (m & (uint32_t)FaultCode::SD_MOUNT_FAIL) return LedFault::SD_MOUNT;
  if (m & (uint32_t)FaultCode::SD_WRITE_FAIL) return LedFault::SD_WRITE;
  if (m & (uint32_t)FaultCode::ADC_FAULT) return LedFault::ADC_FAULT;
  if (m & (uint32_t)FaultCode::IMU_FAULT) return LedFault::IMU_FAULT;
  return LedFault::NONE;
}

LedWarning system_status_get_led_warning() {
  portENTER_CRITICAL(&s_mux);
  uint32_t m = s_warning_mask;
  portEXIT_CRITICAL(&s_mux);
  if (m & (uint32_t)WarningCode::RTC_INVALID) return LedWarning::RTC_INVALID;
  if (m & (uint32_t)WarningCode::RTC_FAULT) return LedWarning::RTC_FAULT;
  if (m & (uint32_t)WarningCode::LOW_BATT) return LedWarning::LOW_BATT;
  if (m & (uint32_t)WarningCode::UNDERLOAD) return LedWarning::UNDERLOAD;
  if (m & (uint32_t)WarningCode::OVERLOAD) return LedWarning::OVERLOAD;
  if (m & (uint32_t)WarningCode::COMPRESSION) return LedWarning::COMPRESSION;
  if (m & (uint32_t)WarningCode::IMU_WARN) return LedWarning::IMU_WARN;
  return LedWarning::NONE;
}
