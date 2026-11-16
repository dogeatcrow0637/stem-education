#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"

#define EPW_WIFI_CONNECTED_BIT BIT0

/* 创建事件组、初始化 Wi-Fi 并开始连接（SSID/密码来自 menuconfig）。 */
esp_err_t wifi_sta_init(void);

/* 等待拿到 IP；超时返回 false。 */
bool wifi_sta_wait_connected(TickType_t ticks_to_wait);

/* 供后续 WebSocket/OTA 任务判断当前网络状态。 */
bool wifi_sta_is_connected(void);
