#include "button.h"

#include "driver/gpio.h"
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

static void toggle_pet(const char *source)
{
    if (bsp_display_lock(100) != ESP_OK) {
        ESP_LOGW(TAG, "%s: display busy, ignored", source);
        return;
    }
    ui_demo_toggle_pet();
    bsp_display_unlock();
    ESP_LOGI(TAG, "%s short press -> pet page", source);
}

static void toggle_apps(const char *source)
{
    if (bsp_display_lock(100) != ESP_OK) {
        ESP_LOGW(TAG, "%s: display busy, ignored", source);
        return;
    }
    ui_demo_toggle_apps();
    bsp_display_unlock();
    ESP_LOGI(TAG, "%s short press -> app desktop", source);
}

static void button_task(void *arg)
{
    (void)arg;
    int low_ticks = 0;
    int pwr_div = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));

        /* BOOT：连续 3 次采样为低（约 60ms）才认一次按下 */
        if (gpio_get_level(BTN_GPIO) == 0) {
            if (low_ticks < BTN_DEBOUNCE_TICKS) {
                low_ticks++;
                if (low_ticks == BTN_DEBOUNCE_TICKS) {
                    toggle_pet("BOOT");
                }
            }
        } else {
            low_ticks = 0;
        }

        if (++pwr_div >= PWR_POLL_EVERY) {
            pwr_div = 0;
            if (power_pwrkey_short_pressed()) {
                toggle_apps("PWR");
            }
        }
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

    power_pwrkey_irq_init();

    xTaskCreate(button_task, "epw_btn", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "buttons ready: BOOT=GPIO%d, PWR=AXP2101 IRQ", BTN_GPIO);
}
