/*
 * 豆包宠物状态桥（设备端）
 *
 * 这条链路刻意做得很薄：
 *   火山方舟 / xiaozhi-server（服务端，持有 Key）
 *        └─ HTTP JSON {"emotion":"happy","text":"..."}
 *              └─ 本文件轮询 → ui_pet_set_state_by_name()
 *
 * 设备不保存任何云 Key，也不在 UI 线程做网络请求；服务端可以随时把
 * mock 数据换成真正的豆包回复。
 */
#include "doubao_bridge.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"
#include "ui.h"
#include "wifi.h"

static const char *TAG = "epw-doubao";

static char s_rx[640];
static int s_fail_count;
static bool s_rx_overflow;

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL) {
        size_t used = strlen(s_rx);
        size_t n = (size_t)evt->data_len;
        if (!s_rx_overflow && n < sizeof(s_rx) - used) {
            memcpy(s_rx + used, evt->data, n);
            s_rx[used + n] = '\0';
        } else {
            s_rx_overflow = true;
        }
    }
    return ESP_OK;
}

static void bridge_task(void *arg)
{
    (void)arg;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_EPW_PET_BRIDGE_INTERVAL_MS));

        if (wifi_get_state() != EPW_WIFI_CONNECTED) {
            continue;
        }

        s_rx[0] = '\0';
        s_rx_overflow = false;
        esp_http_client_config_t cfg = {
            .url = CONFIG_EPW_PET_BRIDGE_URL,
            .method = HTTP_METHOD_GET,
            .timeout_ms = 5000,
            .event_handler = http_event_cb,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (client == NULL) {
            continue;
        }

        esp_err_t err = esp_http_client_perform(client);
        int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
        esp_http_client_cleanup(client);

        cJSON *json = (err == ESP_OK && status == 200 && !s_rx_overflow)
                          ? cJSON_ParseWithLengthOpts(s_rx, strlen(s_rx) + 1, NULL, true) : NULL;
        const cJSON *emotion = cJSON_GetObjectItemCaseSensitive(json, "emotion");
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(json, "text");
        if (cJSON_IsObject(json) && cJSON_IsString(emotion) && cJSON_IsString(text)) {
            if (bsp_display_lock(300) == ESP_OK) {
                ui_pet_set_state_by_name(emotion->valuestring, text->valuestring);
                bsp_display_unlock();
            }
            s_fail_count = 0;
        } else {
            s_fail_count++;
            /* 服务端没开时不要每 8 秒刷屏 */
            if (s_fail_count <= 3 || (s_fail_count % 30) == 0) {
                ESP_LOGW(TAG, "bridge request failed: err=%s http=%d",
                         esp_err_to_name(err), status);
            }
        }
        cJSON_Delete(json);
    }
}

void doubao_bridge_init(void)
{
    if (strlen(CONFIG_EPW_PET_BRIDGE_URL) == 0) {
        ESP_LOGI(TAG, "no bridge URL configured; local scenario engine only");
        return;
    }

    xTaskCreate(bridge_task, "epw_doubao", 6144, NULL, 3, NULL);
    ESP_LOGI(TAG, "bridge started: %s every %dms",
             CONFIG_EPW_PET_BRIDGE_URL, CONFIG_EPW_PET_BRIDGE_INTERVAL_MS);
}
