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

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL) {
        size_t used = strlen(s_rx);
        size_t n = (size_t)evt->data_len;
        if (used + n < sizeof(s_rx) - 1) {
            memcpy(s_rx + used, evt->data, n);
            s_rx[used + n] = '\0';
        }
    }
    return ESP_OK;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static size_t utf8_put(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* 极简 JSON 字符串取值：只处理本项目需要的两个扁平字段。 */
static bool json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    if (out_len == 0) {
        return false;
    }
    out[0] = '\0';

    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (p == NULL) {
        return false;
    }
    p = strchr(p + strlen(pat), ':');
    if (p == NULL) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;

    size_t o = 0;
    while (*p != '\0' && *p != '"') {
        uint32_t cp = (uint8_t)*p++;
        if (cp == '\\' && *p != '\0') {
            char esc = *p++;
            switch (esc) {
            case 'n':
                cp = '\n';
                break;
            case 'r':
                cp = '\r';
                break;
            case 't':
                cp = '\t';
                break;
            case 'u': {
                uint32_t v = 0;
                int used = 0;
                bool ok = true;
                for (int i = 0; i < 4; i++) {
                    if (p[i] == '\0') {
                        ok = false;
                        break;
                    }
                    int h = hex_val(p[i]);
                    if (h < 0) {
                        ok = false;
                        break;
                    }
                    v = (v << 4) | (uint32_t)h;
                    used++;
                }
                p += used;
                cp = ok ? v : '?';
                break;
            }
            default:
                cp = (uint8_t)esc;
                break;
            }
        }

        char tmp[4];
        size_t n = utf8_put(cp, tmp);
        if (o + n < out_len) {
            memcpy(out + o, tmp, n);
            o += n;
        }
    }
    out[o] = '\0';
    return true;
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

        if (err == ESP_OK && status == 200 && s_rx[0] != '\0') {
            char emotion[32];
            char text[384];
            json_get_string(s_rx, "emotion", emotion, sizeof(emotion));
            json_get_string(s_rx, "text", text, sizeof(text));

            if (bsp_display_lock(300) == ESP_OK) {
                ui_pet_set_state_by_name(emotion, text[0] != '\0' ? text : NULL);
                bsp_display_unlock();
            }
            s_fail_count = 0;
            ESP_LOGI(TAG, "reply: emotion=%s text=%s", emotion, text);
        } else {
            s_fail_count++;
            /* 服务端没开时不要每 8 秒刷屏 */
            if (s_fail_count <= 3 || (s_fail_count % 30) == 0) {
                ESP_LOGW(TAG, "bridge request failed: err=%s http=%d",
                         esp_err_to_name(err), status);
            }
        }
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
