#include "watch_system.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "watch-system";
static uint32_t s_idle_ms = 30000;
static uint32_t s_preset_s = 60;
static int64_t s_remaining_us, s_deadline_us;
static watch_timer_state_t s_timer_state;

static bool idle_timeout_valid(uint32_t timeout)
{
    return timeout == 0 || timeout == 15000 || timeout == 30000 ||
           timeout == 60000 || timeout == 120000 || timeout == 300000;
}

void watch_system_init(void)
{
    nvs_handle_t handle;
    if (nvs_open("system", NVS_READONLY, &handle) == ESP_OK) {
        uint32_t timeout;
        if (nvs_get_u32(handle, "idle_ms", &timeout) == ESP_OK && idle_timeout_valid(timeout))
            s_idle_ms = timeout;
        nvs_close(handle);
    }

}

uint32_t watch_idle_timeout_ms(void)
{
    return s_idle_ms;
}

esp_err_t watch_set_idle_timeout_ms(uint32_t timeout)
{
    if (!idle_timeout_valid(timeout)) return ESP_ERR_INVALID_ARG;
    if (timeout == s_idle_ms) return ESP_OK;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("system", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, "idle_ms", timeout);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) s_idle_ms = timeout;
    return err;
}

bool watch_timer_select(uint32_t seconds)
{
    if (s_timer_state == WATCH_TIMER_RUNNING || s_timer_state == WATCH_TIMER_PAUSED) return false;
    if (seconds != 60 && seconds != 180 && seconds != 300 &&
        seconds != 600 && seconds != 1800 && seconds != 3600) return false;
    watch_timer_reset();
    s_preset_s = seconds;
    return true;
}

uint32_t watch_timer_remaining(void)
{
    if (s_timer_state == WATCH_TIMER_IDLE) return s_preset_s;
    if (s_timer_state == WATCH_TIMER_FINISHED) return 0;
    int64_t us = s_timer_state == WATCH_TIMER_RUNNING ?
        s_deadline_us - esp_timer_get_time() : s_remaining_us;
    return us > 0 ? (uint32_t)((us + 999999) / 1000000) : 0;
}

watch_timer_state_t watch_timer_state(void)
{
    return s_timer_state;
}

bool watch_system_tick(void)
{
    int64_t now = esp_timer_get_time();
    bool expired = s_timer_state == WATCH_TIMER_RUNNING && now >= s_deadline_us;
    if (expired) {
        s_timer_state = WATCH_TIMER_FINISHED;
        ESP_LOGI(TAG, "countdown finished");
    }
    return expired;
}

void watch_timer_start(void)
{
    if (s_timer_state == WATCH_TIMER_RUNNING) return;
    int64_t us = s_timer_state == WATCH_TIMER_PAUSED ? s_remaining_us : (int64_t)s_preset_s * 1000000;
    s_deadline_us = esp_timer_get_time() + us;
    s_timer_state = WATCH_TIMER_RUNNING;
}

void watch_timer_pause(void)
{
    if (s_timer_state != WATCH_TIMER_RUNNING) return;
    s_remaining_us = s_deadline_us - esp_timer_get_time();
    if (s_remaining_us <= 0) return; /* 交给下一次 tick 处理到点，避免吞掉提醒。 */
    s_timer_state = WATCH_TIMER_PAUSED;
}

void watch_timer_reset(void)
{
    s_remaining_us = 0;
    s_timer_state = WATCH_TIMER_IDLE;
}
