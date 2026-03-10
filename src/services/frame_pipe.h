#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "format/log_format.h"

// 500 Hz frame queue produced by ADC task and consumed by logger task.
extern QueueHandle_t g_frame_q;

void frame_pipe_set_mark_next();
bool frame_pipe_consume_mark_next();
void frame_pipe_notify_drop(uint32_t now_ms);
bool frame_pipe_should_set_dropped(uint32_t now_ms);
