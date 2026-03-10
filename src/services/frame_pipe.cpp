#include "services/frame_pipe.h"
#include <cstdint>

QueueHandle_t g_frame_q = nullptr;

static volatile bool s_mark_next = false;
static volatile uint32_t s_last_drop_ms = 0;
static volatile uint32_t s_last_pressure_ms = 0;
static volatile uint32_t s_drop_count = 0;
static volatile uint16_t s_max_queue_depth = 0;
static constexpr uint32_t DROPPED_FLAG_MS = 1000;
static constexpr uint32_t SD_WARN_FLAG_MS = 1000;

void frame_pipe_set_mark_next() { s_mark_next = true; }
bool frame_pipe_consume_mark_next() {
  if (!s_mark_next) return false;
  s_mark_next = false;
  return true;
}
void frame_pipe_notify_drop(uint32_t now_ms) {
  s_last_drop_ms = now_ms;
  s_last_pressure_ms = now_ms;
  s_drop_count = s_drop_count + 1;
}
bool frame_pipe_should_set_dropped(uint32_t now_ms) {
  if (s_last_drop_ms == 0) return false;
  return (now_ms - s_last_drop_ms) < DROPPED_FLAG_MS;
}

void frame_pipe_note_queue_depth(uint32_t now_ms, UBaseType_t depth) {
  if (depth > s_max_queue_depth) s_max_queue_depth = (uint16_t)depth;
  if (depth >= FRAME_QUEUE_PRESSURE_WARN) s_last_pressure_ms = now_ms;
}

bool frame_pipe_should_set_sd_warn(uint32_t now_ms) {
  if (s_last_pressure_ms == 0) return false;
  return (now_ms - s_last_pressure_ms) < SD_WARN_FLAG_MS;
}

uint32_t frame_pipe_get_drop_count() { return s_drop_count; }
uint16_t frame_pipe_get_max_queue_depth() { return s_max_queue_depth; }
