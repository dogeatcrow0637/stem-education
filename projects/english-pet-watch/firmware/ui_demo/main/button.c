#include "button.h"

#include <stdatomic.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "voice_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "power.h"
#include "ui.h"

/* 板载 BOOT 键：GPIO0，外部 10K 上拉，按下为低。 */
#define BTN_GPIO            GPIO_NUM_0
#define BTN_POLL_MS         20
#define BTN_DEBOUNCE_TICKS  (60 / BTN_POLL_MS)
/* PWR 键接 AXP2101 PWRON，通过 IRQ 状态寄存器轮询，每 100ms 一次。 */
#define PWR_POLL_EVERY      5

static const char *TAG = "epw-button";
static atomic_bool s_shutdown_requested;
static bool s_pwr_down, s_pwr_fired;
static uint32_t s_pwr_started;

static void toggle_pet(const char *source)
{
    if (bsp_display_lock(100) != ESP_OK) {
        ESP_LOGW(TAG, "%s: display busy, ignored", source);
        return;
    }
    if (ui_power_menu_cancel()) {
        bsp_display_unlock();
        return;
    }
    bool woke = ui_activity();
    if (!woke) {
        ui_demo_toggle_pet();
    }
    bsp_display_unlock();
    ESP_LOGI(TAG, "%s short press -> %s", source, woke ? "wake screen" : "pet page");
}

static void toggle_apps(const char *source)
{
    if (bsp_display_lock(100) != ESP_OK) {
        ESP_LOGW(TAG, "%s: display busy, ignored", source);
        return;
    }
    if (ui_power_menu_cancel()) {
        bsp_display_unlock();
        return;
    }
    bool woke = ui_activity();
    if (!woke) {
        ui_demo_toggle_apps();
    }
    bsp_display_unlock();
    ESP_LOGI(TAG, "%s short press -> %s", source, woke ? "wake screen" : "app desktop");
}

void button_request_shutdown(void)
{
    atomic_store(&s_shutdown_requested, true);
}

static void shutdown_service(void)
{
    voice_client_set_active(false);
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_err_t err = power_shutdown();
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGE(TAG, "power-off incomplete: %s", esp_err_to_name(err));
    /* PMU 失败或 USB 仍直接供电时，不把亮屏失败伪装成关机成功。 */
    esp_wifi_start();
    if (bsp_display_lock(-1) == ESP_OK) {
        ui_power_shutdown_failed();
        bsp_display_unlock();
    }
}

static void pwr_key_poll(void)
{
    uint8_t events;
    if (power_key_read(&events) != ESP_OK) {
        s_pwr_down = false; /* 总线故障不能被当成长按 */
        return;
    }
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (events & POWER_KEY_PRESS) {
        s_pwr_down = true;
        s_pwr_fired = false;
        s_pwr_started = now;
    }
    if (events & (POWER_KEY_RELEASE | POWER_KEY_SHORT)) {
        if ((events & POWER_KEY_SHORT) && !s_pwr_fired) toggle_apps("PWR");
        s_pwr_down = false;
    }
    if (s_pwr_down && !s_pwr_fired && (uint32_t)(now - s_pwr_started) >= 3000u) {
        if (bsp_display_lock(100) == ESP_OK) {
            ui_power_menu_open();
            bsp_display_unlock();
            s_pwr_fired = true;
            ESP_LOGI(TAG, "PWR 3s hold -> shutdown confirmation");
        }
    }
}

static void button_task(void *arg)
{
    (void)arg;
    int low_ticks = 0;
    int pwr_div = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
        if (!s_pwr_down && atomic_exchange(&s_shutdown_requested, false)) shutdown_service();

        if (gpio_get_level(BTN_GPIO) == 0) {
            if (low_ticks < BTN_DEBOUNCE_TICKS) {
                if (++low_ticks == BTN_DEBOUNCE_TICKS) toggle_pet("BOOT");
            }
        } else {
            low_ticks = 0;
        }

        if (++pwr_div < PWR_POLL_EVERY) continue;
        pwr_div = 0;
        pwr_key_poll();
    }
}

void button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    uint8_t ignored;
    power_key_read(&ignored); /* 吞掉开机这次长按，不触发菜单 */

    xTaskCreate(button_task, "epw_btn", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "buttons ready: BOOT=GPIO%d, PWR=AXP2101 IRQ", BTN_GPIO);
}
