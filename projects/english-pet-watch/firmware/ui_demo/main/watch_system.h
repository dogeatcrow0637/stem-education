#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    WATCH_TIMER_IDLE, WATCH_TIMER_RUNNING, WATCH_TIMER_PAUSED, WATCH_TIMER_FINISHED
} watch_timer_state_t;

void watch_system_init(void);
uint32_t watch_idle_timeout_ms(void);
esp_err_t watch_set_idle_timeout_ms(uint32_t timeout);
bool watch_timer_select(uint32_t seconds);
void watch_timer_start(void);
void watch_timer_pause(void);
void watch_timer_reset(void);
uint32_t watch_timer_remaining(void);
watch_timer_state_t watch_timer_state(void);
/* 由 UI 定时器轮询；仅在本次到点时返回 true，后台倒计时不依赖页面。 */
bool watch_system_tick(void);
