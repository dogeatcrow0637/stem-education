/*
 * 英语宠物怀表 · 自研固件 V0.1
 *
 * 当前只做两件事：
 *   1. 连接 2.4GHz Wi-Fi；
 *   2. 向 xiaozhi-server 的 OTA 端点上报版本，有更新就下载并重启。
 *
 * 不包含屏幕、音频、唤醒与 WebSocket 对话；这些在 V0.2 起按 docs/06 的路线实现。
 */

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "ota_client.h"
#include "wifi_sta.h"

static const char *TAG = "epw";

static esp_err_t check_and_maybe_update(void)
{
    epw_ota_result_t result = {0};
    esp_err_t err = epw_ota_check(&result);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA 检查失败: %s", esp_err_to_name(err));
        return err;
    }

    if (result.websocket_url[0] != '\0') {
        ESP_LOGI(TAG, "服务端 WebSocket 地址: %s", result.websocket_url);
    }

    if (!result.update_available) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "准备升级到 %s", result.version);
    err = epw_ota_apply(result.url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "升级失败，继续使用当前固件: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "升级成功，3 秒后重启");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "英语宠物怀表 · 自研固件");
    ESP_LOGI(TAG, "版本: %s", app->version);
    ESP_LOGI(TAG, "型号: %s", CONFIG_EPW_DEVICE_MODEL);
    ESP_LOGI(TAG, "OTA : %s", CONFIG_EPW_OTA_URL);
    ESP_LOGI(TAG, "分区: %s", running ? running->label : "unknown");
    ESP_LOGI(TAG, "固件编译时间: %s %s", app->date, app->time);
    ESP_LOGI(TAG, "================================");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (strlen(CONFIG_EPW_WIFI_SSID) == 0) {
        ESP_LOGE(TAG, "还没有配置 Wi-Fi。");
        ESP_LOGE(TAG, "请执行 idf.py menuconfig -> English Pet Watch -> Wi-Fi SSID / Password 后重新编译烧录。");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }

    ESP_ERROR_CHECK(wifi_sta_init());

    while (true) {
        if (!wifi_sta_wait_connected(pdMS_TO_TICKS(30000))) {
            ESP_LOGW(TAG, "等待 Wi-Fi 连接超时，30 秒后重试");
            continue;
        }

        ESP_LOGI(TAG, "Wi-Fi 已连接，剩余内存: %lu bytes",
                 (unsigned long)esp_get_free_heap_size());
        check_and_maybe_update();

        ESP_LOGI(TAG, "%d 秒后再次检查更新", CONFIG_EPW_OTA_CHECK_INTERVAL_S);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_EPW_OTA_CHECK_INTERVAL_S * 1000ULL));
    }
}
