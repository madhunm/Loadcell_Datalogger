#pragma once
#include <cstdint>
#include "format/log_format.h"

bool logger_begin();
void start_logger_task();

bool logger_start_session(const PdlHeaderV1& hdr);
void logger_stop_session();
bool logger_is_logging();

PdlHeaderV1 logger_make_default_header();
