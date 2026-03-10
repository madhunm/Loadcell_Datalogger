#include "services/frame_pipe.h"
#include <cstdint>

QueueHandle_t g_frame_q = nullptr;

static volatile bool s_mark_next = false;
static volatile uint32_t s_last_drop_ms = 0;
static constexpr uint32_t DROPPED_FLAG_MS = 1000;

void frame_pipe_set_mark_next() { s_mark_next = true; }
bool frame_pipe_consume_mark_next() {
  if (!s_mark_next) return false;
  s_mark_next = false;
  return true;
}
void frame_pipe_notify_drop(uint32_t now_ms) { s_last_drop_ms = now_ms; }
bool frame_pipe_should_set_dropped(uint32_t now_ms) {
  if (s_last_drop_ms == 0) return false;
  return (now_ms - s_last_drop_ms) < DROPPED_FLAG_MS;
}
