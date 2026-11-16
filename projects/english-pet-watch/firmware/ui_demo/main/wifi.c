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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "epw-wifi";

#define WIFI_NVS_NS    "epw_wifi"
#define WIFI_NVS_SSID  "ssid"
#define WIFI_NVS_PASS  "pass"

static volatile epw_wifi_state_t s_state = EPW_WIFI_OFF;
static int s_retry_num;
static bool s_hinted_no_ap;
static bool s_sntp_started;
static char s_ip[16] = "--";

static bool s_inited;
static bool s_have_cred;
static bool s_user_connect;  /* 本次连接由用户在配网页发起 */
static bool s_auto_retry;    /* 失败后是否继续自动重连 */
static char s_ssid[33];
static char s_pass[65];

/* 扫描结果：后台任务写入，UI 线程只读；用互斥锁保护 */
static SemaphoreHandle_t s_scan_lock;
static volatile bool s_scan_running;
static volatile bool s_scan_ready;
static epw_wifi_ap_t s_scan_aps[EPW_WIFI_MAX_APS];
static int s_scan_count;

/* -------------------------------------------------------------------------- */
/* NVS 账号                                                                     */
/* -------------------------------------------------------------------------- */

static void creds_load(void)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_ssid);
    if (nvs_get_str(h, WIFI_NVS_SSID, s_ssid, &len) == ESP_OK && s_ssid[0] != '\0') {
        len = sizeof(s_pass);
        if (nvs_get_str(h, WIFI_NVS_PASS, s_pass, &len) != ESP_OK) {
            s_pass[0] = '\0';
        }
        s_have_cred = true;
        ESP_LOGI(TAG, "saved network: %s", s_ssid);
    }
    nvs_close(h);
}

static void creds_save(const char *ssid, const char *password)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "cannot open NVS for saving");
        return;
    }
    nvs_set_str(h, WIFI_NVS_SSID, ssid);
    nvs_set_str(h, WIFI_NVS_PASS, password != NULL ? password : "");
    nvs_commit(h);
    nvs_close(h);
}

static void creds_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_key(h, WIFI_NVS_SSID);
    nvs_erase_key(h, WIFI_NVS_PASS);
    nvs_commit(h);
    nvs_close(h);
}

/* -------------------------------------------------------------------------- */
/* 网络事件                                                                     */
/* -------------------------------------------------------------------------- */

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

static void apply_sta_config(void)
{
    wifi_config_t cfg = {0};
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", s_ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", s_pass);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
}

static bool disconnect_is_fatal(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_CONNECTION_FAIL:
        return true;
    default:
        return false;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_have_cred) {
            s_state = EPW_WIFI_CONNECTING;
            esp_wifi_connect();
        } else {
            s_state = EPW_WIFI_OFF;
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "disconnected, reason=%d", evt->reason);
        if (evt->reason == WIFI_REASON_NO_AP_FOUND && !s_hinted_no_ap) {
            s_hinted_no_ap = true;
            ESP_LOGW(TAG, "  reason 201 = SSID not found on 2.4GHz; "
                          "check hotspot is on and 最大兼容性(Maximize Compatibility) enabled");
        }
        snprintf(s_ip, sizeof(s_ip), "--");

        if (!s_have_cred) {
            s_state = EPW_WIFI_OFF;
            return;
        }

        if (s_user_connect) {
            /* 用户手动连接：密码/热点问题直接报失败，别让 UI 一直转圈 */
            if (!disconnect_is_fatal(evt->reason) && s_retry_num < 2) {
                s_retry_num++;
                s_state = EPW_WIFI_CONNECTING;
                esp_wifi_connect();
            } else {
                s_user_connect = false;
                s_auto_retry = false;
                s_state = EPW_WIFI_FAILED;
            }
        } else if (s_auto_retry && s_retry_num < CONFIG_EPW_WIFI_MAX_RETRY) {
            s_retry_num++;
            s_state = EPW_WIFI_CONNECTING;
            esp_wifi_connect();
        } else {
            s_state = EPW_WIFI_FAILED;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "connected, ip=" IPSTR, IP2STR(&evt->ip_info.ip));
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry_num = 0;
        s_user_connect = false;
        s_auto_retry = true; /* 掉线后继续自动重连 */
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
        if (s_state == EPW_WIFI_FAILED && s_have_cred && s_auto_retry) {
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

/* -------------------------------------------------------------------------- */
/* 扫描                                                                         */
/* -------------------------------------------------------------------------- */

static void scan_task(void *arg)
{
    (void)arg;

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };

    epw_wifi_ap_t tmp[EPW_WIFI_MAX_APS * 2];
    int n = 0;

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err == ESP_OK) {
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        if (ap_num > 0) {
            wifi_ap_record_t *recs = calloc(ap_num, sizeof(*recs));
            if (recs != NULL) {
                uint16_t got = ap_num;
                if (esp_wifi_scan_get_ap_records(&got, recs) == ESP_OK) {
                    int max = (int)(sizeof(tmp) / sizeof(tmp[0]));
                    for (uint16_t i = 0; i < got && n < max; i++) {
                        const char *ssid = (const char *)recs[i].ssid;
                        if (ssid[0] == '\0') {
                            continue;
                        }
                        bool dup = false;
                        for (int j = 0; j < n; j++) {
                            if (strcmp(tmp[j].ssid, ssid) == 0) {
                                dup = true;
                                if (recs[i].rssi > tmp[j].rssi) {
                                    tmp[j].rssi = recs[i].rssi;
                                }
                                break;
                            }
                        }
                        if (dup) {
                            continue;
                        }
                        strncpy(tmp[n].ssid, ssid, sizeof(tmp[n].ssid) - 1);
                        tmp[n].ssid[sizeof(tmp[n].ssid) - 1] = '\0';
                        tmp[n].rssi = recs[i].rssi;
                        tmp[n].secure = recs[i].authmode != WIFI_AUTH_OPEN;
                        n++;
                    }
                }
                free(recs);
            }
        }
        esp_wifi_clear_ap_list();
    } else {
        ESP_LOGW(TAG, "ui scan failed: %s", esp_err_to_name(err));
    }

    /* 信号强的放前面（简单插入排序，最多 32 条） */
    for (int i = 1; i < n; i++) {
        epw_wifi_ap_t key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j].rssi < key.rssi) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }

    if (n > EPW_WIFI_MAX_APS) {
        n = EPW_WIFI_MAX_APS;
    }
    if (s_scan_lock != NULL) {
        xSemaphoreTake(s_scan_lock, portMAX_DELAY);
    }
    memcpy(s_scan_aps, tmp, (size_t)n * sizeof(tmp[0]));
    s_scan_count = n;
    s_scan_ready = true;
    s_scan_running = false;
    if (s_scan_lock != NULL) {
        xSemaphoreGive(s_scan_lock);
    }

    ESP_LOGI(TAG, "ui scan done: %d AP(s)", n);
    vTaskDelete(NULL);
}

esp_err_t wifi_scan_async(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_scan_running) {
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_running = true;
    s_scan_ready = false;
    s_scan_count = 0;

    if (xTaskCreate(scan_task, "epw_wifi_scan", 4096, NULL, 3, NULL) != pdPASS) {
        s_scan_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool wifi_scan_busy(void)
{
    return s_scan_running;
}

int wifi_scan_copy(epw_wifi_ap_t *out, int max)
{
    if (out == NULL || max <= 0) {
        return -1;
    }
    if (s_scan_running || !s_scan_ready) {
        return -1;
    }
    int n = s_scan_count;
    if (n > max) {
        n = max;
    }
    if (s_scan_lock != NULL) {
        xSemaphoreTake(s_scan_lock, portMAX_DELAY);
    }
    memcpy(out, s_scan_aps, (size_t)n * sizeof(out[0]));
    if (s_scan_lock != NULL) {
        xSemaphoreGive(s_scan_lock);
    }
    return n;
}

/* -------------------------------------------------------------------------- */
/* 对外 API                                                                     */
/* -------------------------------------------------------------------------- */

void wifi_init_sta(void)
{
    if (s_inited) {
        return;
    }
    s_inited = true;

    if (s_scan_lock == NULL) {
        s_scan_lock = xSemaphoreCreateMutex();
    }

    creds_load();
    if (!s_have_cred) {
        /* 没有 NVS 账号时回退到编译期配置，方便老固件继续工作 */
        if (strlen(CONFIG_EPW_WIFI_SSID) > 0) {
            strncpy(s_ssid, CONFIG_EPW_WIFI_SSID, sizeof(s_ssid) - 1);
            strncpy(s_pass, CONFIG_EPW_WIFI_PASSWORD, sizeof(s_pass) - 1);
            s_have_cred = true;
        }
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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    esp_err_t err = esp_wifi_set_country_code("CN", true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "country code not set: %s", esp_err_to_name(err));
    }

    if (s_have_cred) {
        apply_sta_config();
        s_auto_retry = true;
        ESP_LOGI(TAG, "connecting to hotspot, ssid=%s", s_ssid);
    } else {
        ESP_LOGI(TAG, "no saved network; open the Network app to configure");
        s_state = EPW_WIFI_OFF;
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(reconnect_task, "epw_wifi_retry", 3072, NULL, 3, NULL);
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

const char *wifi_get_ssid(void)
{
    return s_ssid;
}

bool wifi_has_credentials(void)
{
    return s_have_cred;
}

esp_err_t wifi_connect_to(const char *ssid, const char *password)
{
    if (!s_inited) {
        wifi_init_sta();
    }
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    snprintf(s_pass, sizeof(s_pass), "%s", password != NULL ? password : "");
    s_have_cred = true;
    s_user_connect = true;
    s_auto_retry = false;
    s_retry_num = 0;
    s_hinted_no_ap = false;
    s_state = EPW_WIFI_CONNECTING;

    creds_save(s_ssid, s_pass);

    esp_wifi_disconnect();
    wifi_config_t cfg = {0};
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", s_ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", s_pass);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_connect();
    ESP_LOGI(TAG, "connect requested: %s err=%s", s_ssid, esp_err_to_name(err));
    return err;
}

void wifi_disconnect(void)
{
    if (!s_inited) {
        return;
    }
    s_auto_retry = false;
    s_user_connect = false;
    esp_wifi_disconnect();
    s_state = EPW_WIFI_OFF;
    snprintf(s_ip, sizeof(s_ip), "--");
}

void wifi_forget(void)
{
    if (!s_inited) {
        return;
    }
    s_auto_retry = false;
    s_user_connect = false;
    s_have_cred = false;
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    creds_erase();
    esp_wifi_disconnect();
    s_state = EPW_WIFI_OFF;
    snprintf(s_ip, sizeof(s_ip), "--");
}
