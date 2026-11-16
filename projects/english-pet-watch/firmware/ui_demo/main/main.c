#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "audio.h"
#include "button.h"
#include "doubao_bridge.h"
#include "power.h"
#include "shot.h"
#include "ui.h"
#include "wifi.h"

/* IDF 的 time.h 未在 gnu23 下声明 tzset，但 esp_libc 已实现。 */
extern void tzset(void);

static const char *TAG = "epw-ui";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "English Pet Watch UI demo boot");

    /* SNTP 同步后使用北京时间（无网络时 UI 回退到内置时钟） */
    setenv("TZ", "CST-8", 1);
    tzset();

    /* 屏幕 + 触摸 + LVGL */
    bsp_display_start();
    bsp_display_brightness_set(80);

    /* 音频输出（ES8311）：音量页现在会真正调节扬声器 */
    audio_init();

    /* AXP2101 电量 */
    power_init();

    /* UI */
    bsp_display_lock(-1);
    ui_init();
    bsp_display_unlock();

    /* 物理按键：BOOT / PWR 短按切换功能菜单 */
    button_init();

    /* 热点连接在 UI 起来之后再做，避免挡住首屏 */
    wifi_init_sta();

    /* 可选：豆包宠物状态桥（服务端返回 emotion + text） */
    doubao_bridge_init();

#if CONFIG_EPW_BOOT_SHOTS
    /* 演示/验收：开机自动巡页并截图（正常固件关闭） */
    xTaskCreate(shot_capture_demo_pages, "epw_shot", 8192, NULL, 4, NULL);
#endif

    ESP_LOGI(TAG, "UI ready");
}
