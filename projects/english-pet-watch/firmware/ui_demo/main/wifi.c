#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "epw-wifi";

static volatile epw_wifi_state_t s_state = EPW_WIFI_OFF;
static int s_retry_num;
static bool s_hinted_no_ap;
static bool s_sntp_started;
static char s_ip[16] = "--";

/* 联网后启动 SNTP，表盘显示真实时间；失败时 UI 自动用内置时钟兜底。 */
static void sntp_start_once(void)
{
    if (s_sntp_started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.wait_for_sync = false; /* 不阻塞 Wi-Fi 事件任务 */
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err == ESP_OK) {
        s_sntp_started = true;
        ESP_LOGI(TAG, "SNTP started");
    } else {
        ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(err));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_state = EPW_WIFI_CONNECTING;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "disconnected, reason=%d", evt->reason);
        if (evt->reason == WIFI_REASON_NO_AP_FOUND && !s_hinted_no_ap) {
            s_hinted_no_ap = true;
            ESP_LOGW(TAG, "  reason 201 = SSID not found on 2.4GHz; "
                          "check hotspot is on and 最大兼容性(Maximize Compatibility) enabled");
        }
        s_state = EPW_WIFI_CONNECTING;
        if (s_retry_num < CONFIG_EPW_WIFI_MAX_RETRY) {
            s_retry_num++;
            esp_wifi_connect();
        } else {
            s_state = EPW_WIFI_FAILED;
        }
        snprintf(s_ip, sizeof(s_ip), "--");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "connected, ip=" IPSTR, IP2STR(&evt->ip_info.ip));
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry_num = 0;
        s_state = EPW_WIFI_CONNECTED;
        sntp_start_once();
    }
}

/* 扫描并打印 2.4GHz 可见 AP，便于判断热点是否开启/是否为 5GHz。 */
static void scan_and_log(void)
{
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return;
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num == 0) {
        ESP_LOGW(TAG, "scan: no 2.4GHz AP in range");
        return;
    }
    if (ap_num > 16) {
        ap_num = 16;
    }

    wifi_ap_record_t *recs = calloc(ap_num, sizeof(*recs));
    if (recs == NULL) {
        return;
    }

    uint16_t got = ap_num;
    if (esp_wifi_scan_get_ap_records(&got, recs) == ESP_OK) {
        ESP_LOGI(TAG, "scan: %u 2.4GHz AP(s) visible", (unsigned)got);
        for (uint16_t i = 0; i < got; i++) {
            ESP_LOGI(TAG, "  [%02u] %-24s rssi=%3d ch=%2d auth=%d",
                     (unsigned)(i + 1), (const char *)recs[i].ssid,
                     recs[i].rssi, recs[i].primary, (int)recs[i].authmode);
        }
    }
    free(recs);
    esp_wifi_clear_ap_list();
}

static void reconnect_task(void *arg)
{
    (void)arg;
    bool scanned = false;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        if (s_state == EPW_WIFI_FAILED) {
            /* 首次失败先扫描一次，确认 SSID 是否在 2.4GHz 可见 */
            if (!scanned) {
                scan_and_log();
                scanned = true;
            }
            ESP_LOGI(TAG, "retrying hotspot connection");
            s_retry_num = 0;
            s_state = EPW_WIFI_CONNECTING;
            esp_wifi_connect();
        }
    }
}

void wifi_init_sta(void)
{
    if (strlen(CONFIG_EPW_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "no SSID configured; Wi-Fi stays off");
        s_state = EPW_WIFI_OFF;
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, CONFIG_EPW_WIFI_SSID, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, CONFIG_EPW_WIFI_PASSWORD, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    esp_err_t err = esp_wifi_set_country_code("CN", true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "country code not set: %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(reconnect_task, "epw_wifi_retry", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "connecting to hotspot, ssid=%s", CONFIG_EPW_WIFI_SSID);
}

epw_wifi_state_t wifi_get_state(void)
{
    return s_state;
}

const char *wifi_get_status_text(void)
{
    switch (s_state) {
    case EPW_WIFI_CONNECTING:
        return "CONNECTING";
    case EPW_WIFI_CONNECTED:
        return "ONLINE";
    case EPW_WIFI_FAILED:
        return "FAILED";
    case EPW_WIFI_OFF:
    default:
        return "OFF";
    }
}

const char *wifi_get_ip_str(void)
{
    return s_ip;
}
