#pragma once
#include <cstdint>
#include <cstddef>
#include "format/log_format.h"

bool logger_begin();
void start_logger_task();

bool logger_start_session(const PdlHeaderV1& hdr, bool rtc_valid, uint32_t rtc_epoch);
void logger_stop_session();
bool logger_is_logging();

bool logger_has_last_bin();
bool logger_get_last_bin_path(char* buf, size_t len);

bool logger_export_latest_to_csv();

PdlHeaderV1 logger_make_default_header();
