#pragma once
#include <cstdint>
#include <cstddef>
#include "format/log_format.h"

bool logger_begin();
void start_logger_task();

bool logger_start_session(const PdlHeaderV1& hdr, bool rtc_valid, uint32_t rtc_epoch);
void logger_stop_session();
bool logger_is_logging();
bool logger_is_busy();
/** True if no session is active and no drain/finalization is in progress (safe to start). */
bool logger_can_start();

bool logger_has_last_bin();
bool logger_get_last_bin_path(char* buf, size_t len);
bool logger_take_pending_auto_export_path(char* buf, size_t len);

bool logger_export_bin_to_csv(const char* bin_path);
bool logger_export_latest_to_csv();

PdlHeaderV1 logger_make_default_header();

void logger_set_tare_result(uint16_t tare_frames, int32_t tare_adc_code, uint16_t tare_duration_ms);
