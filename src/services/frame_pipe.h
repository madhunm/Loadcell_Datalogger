#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "format/log_format.h"

// 500 Hz frame queue produced by ADC task and consumed by logger task.
extern QueueHandle_t g_frame_q;
static constexpr UBaseType_t FRAME_QUEUE_DEPTH = 1024;
static constexpr UBaseType_t FRAME_QUEUE_PRESSURE_WARN = (FRAME_QUEUE_DEPTH * 3) / 4;

void frame_pipe_set_mark_next();
bool frame_pipe_consume_mark_next();
void frame_pipe_notify_drop(uint32_t now_ms);
bool frame_pipe_should_set_dropped(uint32_t now_ms);
void frame_pipe_note_queue_depth(uint32_t now_ms, UBaseType_t depth);
bool frame_pipe_should_set_sd_warn(uint32_t now_ms);
uint32_t frame_pipe_get_drop_count();
uint16_t frame_pipe_get_max_queue_depth();
