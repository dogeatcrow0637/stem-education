#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EPW_WIFI_OFF = 0,
    EPW_WIFI_CONNECTING,
    EPW_WIFI_CONNECTED,
    EPW_WIFI_FAILED,
} epw_wifi_state_t;

/** 一条扫描到的 2.4GHz 热点。 */
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} epw_wifi_ap_t;

#define EPW_WIFI_MAX_APS 16

/** 启动 Wi-Fi STA：优先使用 NVS 保存的账号，其次使用编译期配置。 */
void wifi_init_sta(void);

/** 当前连接状态。 */
epw_wifi_state_t wifi_get_state(void);

/** 给 UI 用的短状态文本：OFF / CONNECTING / ONLINE / FAILED。 */
const char *wifi_get_status_text(void);

/** 当前 IPv4 地址；未连接时返回 "--"。 */
const char *wifi_get_ip_str(void);

/** 当前（或最近一次配置的）SSID；没有账号时返回空串。 */
const char *wifi_get_ssid(void);

/** 是否已有可用账号。 */
bool wifi_has_credentials(void);

/** 启动一次后台扫描；已在扫描时返回 ESP_ERR_INVALID_STATE。 */
esp_err_t wifi_scan_async(void);

/** 扫描是否仍在进行。 */
bool wifi_scan_busy(void);

/**
 * 拷贝最近一次扫描结果（按信号强度从强到弱）。
 * 扫描进行中返回 -1；扫描失败返回 -2；扫描完成但没有热点返回 0。
 */
int wifi_scan_copy(epw_wifi_ap_t *out, int max);

/** 请求连接 WPA2/WPA3 或开放热点；获得 IP 后才保存账号。 */
esp_err_t wifi_connect_to(const char *ssid, const char *password);

/** 主动断开，不再自动重连。 */
void wifi_disconnect(void);

/** 忘记已保存的热点。 */
void wifi_forget(void);

#ifdef __cplusplus
}
#endif
