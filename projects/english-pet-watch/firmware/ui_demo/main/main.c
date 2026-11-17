#include <stdio.h>
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "button.h"
#include "doubao_bridge.h"
#include "nursery.h"
#include "power.h"
#include "shot.h"
#include "ui.h"
#include "voice_client.h"
#include "wifi.h"
#include "weather.h"

/* IDF 的 time.h 未在 gnu23 下声明 tzset，但 esp_libc 已实现。 */
extern void tzset(void);

static const char *TAG = "epw-ui";

extern void ui_debug_icon_bench_prepare(void);
extern unsigned ui_debug_icon_frame(bool legacy,int x,int y);
static void icon_bench_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(6000));ui_debug_icon_bench_prepare();
    vTaskDelay(pdMS_TO_TICKS(400));
    for(int phase=0;phase<2;phase++) {
        ui_debug_icon_frame(phase==0,-96,-96);
        for(int i=0;i<48;i++) {
            int x=-96+(i%24)*8,y=i<24?x:-x;
            printf("ICON_PERF:%d:%d:%u\n",phase,i,ui_debug_icon_frame(phase==0,x,y));
            vTaskDelay(pdMS_TO_TICKS(25));
        }
    }
    ui_debug_icon_frame(false,0,0);
    shot_dump("apps_continuous");printf("ICONBENCHDONE\n");
    vTaskDelete(NULL);
}
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "English Pet Watch UI demo boot");

    /* 使用北京时间；首次校时前 UI 显示等待状态 */
    setenv("TZ", "CST-8", 1);
    tzset();

    /* 先检查电源键，未按满时不启动显示、联网与音频。 */
    ESP_ERROR_CHECK(power_init());
    ESP_ERROR_CHECK(power_key_configure());
    esp_err_t boot_err = power_boot_wait();
    if (boot_err != ESP_OK) ESP_LOGE(TAG, "power-on check failed: %s", esp_err_to_name(boot_err));

    /* 屏幕 + 触摸 + LVGL */
    bsp_display_start();
    bsp_display_lock(-1);
    ui_boot_logo_show();
    bsp_display_brightness_set(80);
    bsp_display_unlock();

    /* UI */
    bsp_display_lock(-1);
    ui_init();
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(1200));
    bsp_display_lock(-1);
    ui_boot_logo_hide();
    bsp_display_unlock();

    /* 物理按键：BOOT / PWR 短按切换功能菜单 */
    button_init();

    /* 热点连接在 UI 起来之后再做，避免挡住首屏 */
    wifi_init_sta();
    weather_init();
    /* 3S 英语儿歌：曲库在后台拉取，表盘右滑 / 应用桌面进入 */
    nursery_init();

    /* 可选：豆包宠物状态桥（服务端返回 emotion + text） */
    doubao_bridge_init();

    /* 只创建语音控制任务；用户左滑聊天且联网后才启动音频。 */
    voice_client_init();

#if CONFIG_EPW_BOOT_SHOTS
    /* 演示/验收：开机自动巡页并截图（正常固件关闭） */
    xTaskCreate(shot_capture_demo_pages, "epw_shot", 8192, NULL, 4, NULL);
#endif

    ESP_LOGI(TAG, "UI ready");
    xTaskCreateWithCaps(icon_bench_task,"icon_bench",8192,NULL,3,NULL,
                        MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
