#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "format/log_format.h"

// 500 Hz frame queue produced by ADC task and consumed by logger task.
extern QueueHandle_t g_frame_q;
