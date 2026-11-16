#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define EPW_OTA_URL_MAX   256
#define EPW_OTA_VER_MAX   32
#define EPW_WS_URL_MAX    192

typedef struct {
    bool update_available;
    char version[EPW_OTA_VER_MAX];
    char url[EPW_OTA_URL_MAX];
    char websocket_url[EPW_WS_URL_MAX];
} epw_ota_result_t;

/*
 * 向 xiaozhi-server 的 OTA 端点上报设备信息并解析响应。
 * 服务端只在存在更高版本时填 firmware.url；本函数不下载。
 */
esp_err_t epw_ota_check(epw_ota_result_t *out);

/*
 * 从 firmware.url 下载并写入备用 OTA 分区。
 * 成功后调用方需要重启设备，引导程序会切到新分区。
 */
esp_err_t epw_ota_apply(const char *url);
