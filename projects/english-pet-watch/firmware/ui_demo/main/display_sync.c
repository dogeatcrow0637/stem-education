#include "display_sync.h"

#include <stdatomic.h>
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Waveshare 1.75C 原理图：LCD_TE -> GPIO13，面板初始化已开启 0x35。 */
#define DISPLAY_TE_GPIO GPIO_NUM_13
static const char *TAG = "epw-display";
static SemaphoreHandle_t s_te;
static bool s_enabled, s_first_flush, s_logged;
static unsigned s_misses;
static atomic_uint s_last_te_us, s_period_us;

static void IRAM_ATTR display_te_isr(void *arg)
{
    uint32_t now = (uint32_t)esp_timer_get_time();
    uint32_t previous = atomic_exchange(&s_last_te_us, now);
    if (previous) atomic_store(&s_period_us, now - previous);
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(s_te, &wake);
    if (wake) portYIELD_FROM_ISR();
}

static void display_sync_event(lv_event_t *event)
{
    if (!s_enabled) return;
    if (lv_event_get_code(event) == LV_EVENT_RENDER_START) {
        s_first_flush = true;
        return;
    }
    if (!s_first_flush) return;
    s_first_flush = false;
    /* 从有效扫描沿留出提前量，避免空白期开始就写入造成扫描追越。 */
    while (xSemaphoreTake(s_te, 0) == pdTRUE) {}
    if (xSemaphoreTake(s_te, pdMS_TO_TICKS(35)) != pdTRUE) {
        if (++s_misses >= 3) {
            s_enabled = false;
            ESP_LOGW(TAG, "TE signal unavailable; continuing asynchronous refresh");
        }
        return;
    }
    s_misses = 0;
    unsigned period = atomic_load(&s_period_us);
    /* TE 下降沿开始有效扫描；让面板先扫描 1/4 周期再发送。 */
    if (period >= 10000 && period <= 25000)
        vTaskDelay(pdMS_TO_TICKS(period / 4000));
    if (!s_logged && period) {
        s_logged = true;
        ESP_LOGI(TAG, "frame TE sync active: GPIO13 period=%u us, age=%u us", period,
                 (unsigned)((uint32_t)esp_timer_get_time() - atomic_load(&s_last_te_us)));
    }
}

void display_sync_init(lv_display_t *display)
{
    if (!display || s_te) return;
    s_te = xSemaphoreCreateBinary();
    if (!s_te) return;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << DISPLAY_TE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK) {
        err = gpio_install_isr_service(0);
        if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
    }
    if (err == ESP_OK) err = gpio_isr_handler_add(DISPLAY_TE_GPIO, display_te_isr, NULL);
    if (err != ESP_OK) {
        gpio_intr_disable(DISPLAY_TE_GPIO);
        vSemaphoreDelete(s_te);
        s_te = NULL;
        ESP_LOGW(TAG, "TE init failed: %s; continuing asynchronous refresh", esp_err_to_name(err));
        return;
    }
    s_enabled = true;
    lv_display_add_event_cb(display, display_sync_event, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(display, display_sync_event, LV_EVENT_FLUSH_START, NULL);
}
