#include "wifi.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_http_client.h"
#include "http_date.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_rom_crc.h"
#include "sdkconfig.h"

static const char *TAG = "epw-wifi";

#define WIFI_NVS_NS    "epw_wifi"
#define WIFI_NVS_SSID  "ssid"
#define WIFI_NVS_PASS  "pass"

static volatile epw_wifi_state_t s_state = EPW_WIFI_OFF;
static int s_retry_num;
static bool s_hinted_no_ap;
static bool s_sntp_started;
static bool s_time_worker_started;
static char s_ip[16] = "--";

static bool s_inited;
static bool s_have_cred;
static bool s_user_connect;  /* 本次连接由用户在配网页发起 */
static bool s_ignore_leave;
static SemaphoreHandle_t s_control_lock;
static bool s_auto_retry;    /* 失败后是否继续自动重连 */
static char s_ssid[33];
static char s_pass[65];

/* 扫描结果：后台任务写入，UI 线程只读；用互斥锁保护 */
static SemaphoreHandle_t s_scan_lock;
static volatile bool s_scan_running;
static volatile bool s_scan_ready;
static epw_wifi_ap_t s_scan_aps[EPW_WIFI_MAX_APS];
static int s_scan_count;
static bool s_scan_failed;

/* -------------------------------------------------------------------------- */
/* NVS 账号                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    char ssid[33];
    char password[65];
} wifi_saved_cred_t;

static void creds_key(const char *ssid, char key[16])
{
    /* NVS 键最多 15 字符；读取时仍核对完整 SSID，避免散列冲突误用密码。 */
    snprintf(key, 16, "ap%08lx", (unsigned long)esp_rom_crc32_le(0, (const uint8_t *)ssid, strlen(ssid)));
}

static bool creds_read(nvs_handle_t h, const char *ssid, wifi_saved_cred_t *cred)
{
    char key[16];
    creds_key(ssid, key);
    size_t len = sizeof(*cred);
    if (nvs_get_blob(h, key, cred, &len) == ESP_OK &&
        len == sizeof(*cred) && memchr(cred->ssid, 0, sizeof(cred->ssid)) &&
        memchr(cred->password, 0, sizeof(cred->password)) && strcmp(cred->ssid, ssid) == 0)
        return true;
    /* 兼容旧固件仅保存的最近一次成功连接。 */
    len = sizeof(cred->ssid);
    if (nvs_get_str(h, WIFI_NVS_SSID, cred->ssid, &len) != ESP_OK || strcmp(cred->ssid, ssid))
        return false;
    len = sizeof(cred->password);
    return nvs_get_str(h, WIFI_NVS_PASS, cred->password, &len) == ESP_OK;
}

static esp_err_t creds_store(nvs_handle_t h, const wifi_saved_cred_t *cred)
{
    char key[16];
    creds_key(cred->ssid, key);
    wifi_saved_cred_t previous;
    size_t len = sizeof(previous);
    esp_err_t err = nvs_get_blob(h, key, &previous, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (err == ESP_OK && (len != sizeof(previous) ||
        memcmp(previous.ssid, cred->ssid, sizeof(cred->ssid)))) return ESP_FAIL;
    return nvs_set_blob(h, key, cred, sizeof(*cred));
}

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
    wifi_saved_cred_t cred = {0}, previous = {0};
    char previous_ssid[33] = {0};
    size_t len = sizeof(previous_ssid);
    esp_err_t err = ESP_OK;
    /* 切换网络前把旧格式记录迁入历史，失败连接不会覆盖成功密码。 */
    if (nvs_get_str(h, WIFI_NVS_SSID, previous_ssid, &len) == ESP_OK &&
        previous_ssid[0] && creds_read(h, previous_ssid, &previous))
        err = creds_store(h, &previous);
    snprintf(cred.ssid, sizeof(cred.ssid), "%s", ssid);
    snprintf(cred.password, sizeof(cred.password), "%s", password != NULL ? password : "");
    if (err == ESP_OK) err = creds_store(h, &cred);
    if (err == ESP_OK) err = nvs_set_str(h, WIFI_NVS_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, WIFI_NVS_PASS, password != NULL ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "saving network failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}

static void creds_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    char key[16];
    creds_key(s_ssid, key);
    wifi_saved_cred_t cred;
    size_t len = sizeof(cred);
    if (nvs_get_blob(h, key, &cred, &len) == ESP_OK && len == sizeof(cred) &&
        memchr(cred.ssid, 0, sizeof(cred.ssid)) && strcmp(cred.ssid, s_ssid) == 0)
        nvs_erase_key(h, key);
    nvs_erase_key(h, WIFI_NVS_SSID);
    nvs_erase_key(h, WIFI_NVS_PASS);
    nvs_commit(h);
    nvs_close(h);
}

/* -------------------------------------------------------------------------- */
/* 网络事件                                                                     */
/* -------------------------------------------------------------------------- */

static void time_sync_log(struct timeval *tv)
{
    struct tm now;
    localtime_r(&tv->tv_sec, &now);
    ESP_LOGI(TAG, "time synced: %04d-%02d-%02d %02d:%02d:%02d CST",
             now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
             now.tm_hour, now.tm_min, now.tm_sec);
}

static esp_err_t time_header_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key != NULL &&
        strcasecmp(evt->header_key, "Date") == 0) {
        *(time_t *)evt->user_data = epw_http_date(evt->header_value);
    }
    return ESP_OK;
}

static void time_fallback_task(void *arg)
{
    (void)arg;
    /* 公共服务的固定入口无需 DNS；热点 DNS 故障时仍能取得响应时间。 */
    const char *urls[] = {"http://223.5.5.5", "http://1.1.1.1", "http://www.baidu.com"};
    vTaskDelay(pdMS_TO_TICKS(10000));
    while (time(NULL) < 1704067200) {
        for (size_t i = 0; i < sizeof(urls) / sizeof(urls[0]); i++) {
            if (wifi_get_state() != EPW_WIFI_CONNECTED || time(NULL) >= 1704067200) {
                break;
            }
            time_t timestamp = 0;
            esp_http_client_config_t cfg = {
                .url = urls[i], .method = HTTP_METHOD_HEAD, .timeout_ms = 4000,
                .disable_auto_redirect = true, .event_handler = time_header_cb,
                .user_data = &timestamp,
            };
            esp_http_client_handle_t client = esp_http_client_init(&cfg);
            if (client == NULL) {
                continue;
            }
            esp_err_t err = esp_http_client_perform(client);
            esp_http_client_cleanup(client);
            if (err == ESP_OK && timestamp != 0 && time(NULL) < 1704067200) {
                struct timeval tv = {.tv_sec = timestamp};
                if (settimeofday(&tv, NULL) == 0) {
                    ESP_LOGI(TAG, "HTTP Date fallback succeeded");
                    time_sync_log(&tv);
                }
            }
        }
        if (time(NULL) < 1704067200) {
            ESP_LOGW(TAG, "time sync pending; retrying HTTP Date in 20s");
            vTaskDelay(pdMS_TO_TICKS(20000));
        }
    }
    vTaskDelete(NULL);
}

static void sntp_start_once(void)
{
    if (!s_time_worker_started) {
        s_time_worker_started = xTaskCreate(time_fallback_task, "epw_time", 4096, NULL, 3, NULL) == pdPASS;
    }
    if (s_sntp_started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
#if CONFIG_LWIP_SNTP_MAX_SERVERS >= 3
    cfg.num_of_servers = 3;
    cfg.servers[1] = "ntp.tencent.com";
    cfg.servers[2] = "cn.pool.ntp.org";
#endif
    cfg.wait_for_sync = false; /* 不阻塞 Wi-Fi 事件任务 */
    cfg.sync_cb = time_sync_log;
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
    memcpy(cfg.sta.ssid, s_ssid, strlen(s_ssid));
    memcpy(cfg.sta.password, s_pass, strlen(s_pass));
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

static void configure_dns(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        return;
    }
    esp_netif_dns_info_t dns = {.ip.type = ESP_IPADDR_TYPE_V4};
    dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("223.5.5.5");
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
    dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("119.29.29.29");
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_FALLBACK, &dns);
    ESP_LOGI(TAG, "public backup DNS configured");
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    xSemaphoreTake(s_control_lock, portMAX_DELAY);

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_have_cred && !s_scan_running) {
            s_state = EPW_WIFI_CONNECTING;
            esp_wifi_connect();
        } else {
            s_state = EPW_WIFI_OFF;
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)data;
        if (s_ignore_leave && evt->reason == WIFI_REASON_ASSOC_LEAVE) {
            s_ignore_leave = false;
            goto done;
        }
        ESP_LOGW(TAG, "disconnected, reason=%d", evt->reason);
        if (evt->reason == WIFI_REASON_NO_AP_FOUND && !s_hinted_no_ap) {
            s_hinted_no_ap = true;
            ESP_LOGW(TAG, "  reason 201 = SSID not found on 2.4GHz; "
                          "check hotspot is on and 最大兼容性(Maximize Compatibility) enabled");
        }
        snprintf(s_ip, sizeof(s_ip), "--");

        if (s_scan_running || !s_have_cred || (!s_user_connect && !s_auto_retry)) {
            s_state = EPW_WIFI_OFF;
            goto done;
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
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
            strcmp((const char *)ap.ssid, s_ssid) != 0) {
            goto done; /* 切换网络后忽略旧连接排队的 IP 事件。 */
        }
        ESP_LOGI(TAG, "connected, ip=" IPSTR, IP2STR(&evt->ip_info.ip));
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry_num = 0;
        if (s_user_connect) {
            creds_save(s_ssid, s_pass);
        }
        s_ignore_leave = false;
        s_user_connect = false;
        s_auto_retry = true; /* 掉线后继续自动重连 */
        s_state = EPW_WIFI_CONNECTED;
        configure_dns();
        sntp_start_once();
    }
done:
    xSemaphoreGive(s_control_lock);
}

static void reconnect_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        xSemaphoreTake(s_control_lock, portMAX_DELAY);
        bool retry = !s_scan_running && s_state == EPW_WIFI_FAILED && s_have_cred && s_auto_retry;
        xSemaphoreGive(s_control_lock);
        if (retry) {
            xSemaphoreTake(s_control_lock, portMAX_DELAY);
            if (!s_scan_running && s_state == EPW_WIFI_FAILED && s_have_cred && s_auto_retry) {
                ESP_LOGI(TAG, "retrying hotspot connection");
                s_retry_num = 0;
                s_state = EPW_WIFI_CONNECTING;
                esp_wifi_connect();
            }
            xSemaphoreGive(s_control_lock);
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

    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    if (s_state == EPW_WIFI_CONNECTING) {
        /* 驱动连接期间拒绝扫描；保留账号，暂停本轮连接。 */
        s_ignore_leave = esp_wifi_disconnect() == ESP_OK;
        s_user_connect = false;
        s_state = EPW_WIFI_OFF;
    }
    xSemaphoreGive(s_control_lock);

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
    s_scan_failed = err != ESP_OK;
    s_scan_ready = true;
    if (s_scan_lock != NULL) {
        xSemaphoreGive(s_scan_lock);
    }

    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    s_scan_running = false;
    if (s_state == EPW_WIFI_OFF && s_have_cred && s_auto_retry) {
        s_retry_num = 0;
        s_state = EPW_WIFI_CONNECTING;
        esp_wifi_connect();
    }
    xSemaphoreGive(s_control_lock);

    ESP_LOGI(TAG, "ui scan done: %d AP(s)", n);
    vTaskDelete(NULL);
}

esp_err_t wifi_scan_async(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    if (s_scan_running) {
        xSemaphoreGive(s_control_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_running = true;
    xSemaphoreGive(s_control_lock);
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
    if (s_scan_failed) {
        return -2;
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
    s_control_lock = xSemaphoreCreateMutex();
    if (s_control_lock == NULL) {
        ESP_LOGE(TAG, "Wi-Fi control mutex allocation failed");
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
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

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

esp_err_t wifi_connect_saved(const char *ssid)
{
    if (ssid == NULL || ssid[0] == 0 || strlen(ssid) > 32) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;
    wifi_saved_cred_t cred = {0};
    bool found = creds_read(h, ssid, &cred);
    nvs_close(h);
    if (!found) return ESP_ERR_NOT_FOUND;
    return wifi_connect_to(ssid, cred.password);
}

esp_err_t wifi_connect_to(const char *ssid, const char *password)
{
    const char *pass = password != NULL ? password : "";
    size_t ssid_len = ssid != NULL ? strlen(ssid) : 0;
    size_t pass_len = strlen(pass);
    if (ssid_len == 0 || ssid_len > 32 || pass_len > 64 || (pass_len > 0 && pass_len < 8)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < pass_len; i++) {
        if ((unsigned char)pass[i] < 0x20 || (unsigned char)pass[i] > 0x7E ||
            (pass_len == 64 && !isxdigit((unsigned char)pass[i]))) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!s_inited) {
        wifi_init_sta();
    }
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    if (s_scan_running) {
        xSemaphoreGive(s_control_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(s_ssid, ssid, ssid_len + 1);
    memcpy(s_pass, pass, pass_len + 1);
    s_have_cred = true;
    s_user_connect = true;
    s_auto_retry = false;
    s_retry_num = 0;
    s_hinted_no_ap = false;
    s_state = EPW_WIFI_CONNECTING;

    /* 旧连接的主动断开事件不属于新账号的连接失败。 */
    s_ignore_leave = true;
    if (esp_wifi_disconnect() != ESP_OK) {
        s_ignore_leave = false;
    }
    wifi_config_t cfg = {0};
    memcpy(cfg.sta.ssid, s_ssid, ssid_len);
    memcpy(cfg.sta.password, s_pass, pass_len);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg.sta.pmf_cfg.capable = true;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        s_user_connect = false;
        s_state = EPW_WIFI_FAILED;
    }
    xSemaphoreGive(s_control_lock);
    return err;
}

void wifi_disconnect(void)
{
    if (!s_inited) {
        return;
    }
    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    s_auto_retry = false;
    s_user_connect = false;
    esp_wifi_disconnect();
    s_state = EPW_WIFI_OFF;
    snprintf(s_ip, sizeof(s_ip), "--");
    xSemaphoreGive(s_control_lock);
}

void wifi_forget(void)
{
    if (!s_inited) {
        return;
    }
    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    s_auto_retry = false;
    s_user_connect = false;
    s_have_cred = false;
    creds_erase();
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    esp_wifi_disconnect();
    s_state = EPW_WIFI_OFF;
    snprintf(s_ip, sizeof(s_ip), "--");
    xSemaphoreGive(s_control_lock);
}
