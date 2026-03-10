#include "services/frame_pipe.h"
#include <cstdint>
#include "freertos/portmacro.h"

QueueHandle_t g_frame_q = nullptr;

static volatile bool s_mark_next = false;
static volatile uint32_t s_last_drop_ms = 0;
static volatile uint32_t s_last_pressure_ms = 0;
static volatile uint32_t s_drop_count = 0;
static volatile uint16_t s_max_queue_depth = 0;
static portMUX_TYPE s_frame_pipe_mux = portMUX_INITIALIZER_UNLOCKED;
static constexpr uint32_t DROPPED_FLAG_MS = 1000;
static constexpr uint32_t SD_WARN_FLAG_MS = 1000;

void frame_pipe_set_mark_next() {
  portENTER_CRITICAL(&s_frame_pipe_mux);
  s_mark_next = true;
  portEXIT_CRITICAL(&s_frame_pipe_mux);
}
bool frame_pipe_consume_mark_next() {
  portENTER_CRITICAL(&s_frame_pipe_mux);
  const bool marked = s_mark_next;
  if (marked) s_mark_next = false;
  portEXIT_CRITICAL(&s_frame_pipe_mux);
  return marked;
}
void frame_pipe_notify_drop(uint32_t now_ms) {
  portENTER_CRITICAL(&s_frame_pipe_mux);
  s_last_drop_ms = now_ms;
  s_last_pressure_ms = now_ms;
  s_drop_count = s_drop_count + 1;
  portEXIT_CRITICAL(&s_frame_pipe_mux);
}
bool frame_pipe_should_set_dropped(uint32_t now_ms) {
  portENTER_CRITICAL(&s_frame_pipe_mux);
  const uint32_t last_drop_ms = s_last_drop_ms;
  portEXIT_CRITICAL(&s_frame_pipe_mux);
  if (last_drop_ms == 0) return false;
  return (now_ms - last_drop_ms) < DROPPED_FLAG_MS;
}

void frame_pipe_note_queue_depth(uint32_t now_ms, UBaseType_t depth) {
  portENTER_CRITICAL(&s_frame_pipe_mux);
  if (depth > s_max_queue_depth) s_max_queue_depth = (uint16_t)depth;
  if (depth >= FRAME_QUEUE_PRESSURE_WARN) s_last_pressure_ms = now_ms;
  portEXIT_CRITICAL(&s_frame_pipe_mux);
}

bool frame_pipe_should_set_sd_warn(uint32_t now_ms) {
  portENTER_CRITICAL(&s_frame_pipe_mux);
  const uint32_t last_pressure_ms = s_last_pressure_ms;
  portEXIT_CRITICAL(&s_frame_pipe_mux);
  if (last_pressure_ms == 0) return false;
  return (now_ms - last_pressure_ms) < SD_WARN_FLAG_MS;
}

uint32_t frame_pipe_get_drop_count() {
  portENTER_CRITICAL(&s_frame_pipe_mux);
  const uint32_t drop_count = s_drop_count;
  portEXIT_CRITICAL(&s_frame_pipe_mux);
  return drop_count;
}

uint16_t frame_pipe_get_max_queue_depth() {
  portENTER_CRITICAL(&s_frame_pipe_mux);
  const uint16_t max_queue_depth = s_max_queue_depth;
  portEXIT_CRITICAL(&s_frame_pipe_mux);
  return max_queue_depth;
}
