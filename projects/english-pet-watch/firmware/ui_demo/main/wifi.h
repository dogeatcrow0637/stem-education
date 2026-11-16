#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EPW_WIFI_OFF = 0,
    EPW_WIFI_CONNECTING,
    EPW_WIFI_CONNECTED,
    EPW_WIFI_FAILED,
} epw_wifi_state_t;

/** 启动 Wi-Fi STA 并自动连接编译期配置的热点。 */
void wifi_init_sta(void);

/** 当前连接状态。 */
epw_wifi_state_t wifi_get_state(void);

/** 给 UI 用的短状态文本：OFF / CONNECTING / ONLINE / FAILED。 */
const char *wifi_get_status_text(void);

/** 当前 IPv4 地址；未连接时返回 "--"。 */
const char *wifi_get_ip_str(void);

#ifdef __cplusplus
}
#endif
