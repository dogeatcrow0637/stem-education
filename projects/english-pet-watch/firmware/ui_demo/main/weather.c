#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "ui.h"
#include "wifi.h"
#include "weather.h"

static const char *TAG = "epw-weather";
#define WEATHER_BODY_MAX 4096
#define WEATHER_MAX_AGE 7200

static bool parse_weather(const char *body, time_t now, float *temperature, time_t *published)
{
    cJSON *root = cJSON_ParseWithOpts(body, NULL, true);
    cJSON *station = cJSON_GetObjectItemCaseSensitive(root, "station");
    cJSON *code = cJSON_GetObjectItemCaseSensitive(station, "code");
    cJSON *stamp = cJSON_GetObjectItemCaseSensitive(root, "publish_time");
    cJSON *weather = cJSON_GetObjectItemCaseSensitive(root, "weather");
    cJSON *temp = cJSON_GetObjectItemCaseSensitive(weather, "temperature");
    bool valid = false;
    struct tm date = {.tm_isdst = -1};
    int year, month, used = 0;
    if (cJSON_IsString(code) && !strcmp(code->valuestring, "wdcXE") &&
        cJSON_IsString(stamp) && strlen(stamp->valuestring) == 16 && cJSON_IsNumber(temp) && isfinite(temp->valuedouble) &&
        temp->valuedouble >= -90 && temp->valuedouble <= 60 &&
        sscanf(stamp->valuestring, "%4d-%2d-%2d %2d:%2d%n", &year, &month,
               &date.tm_mday, &date.tm_hour, &date.tm_min, &used) == 5 &&
        used == 16 && stamp->valuestring[used] == '\0' && year >= 2024 && year <= 2099) {
        date.tm_year = year - 1900;
        date.tm_mon = month - 1;
        time_t value = mktime(&date); /* 来源时间与系统 TZ 均为北京时间。 */
        char normalized[24];
        strftime(normalized, sizeof(normalized), "%Y-%m-%d %H:%M", &date);
        if (!strcmp(normalized, stamp->valuestring) && value <= now + 300 &&
            now - value <= WEATHER_MAX_AGE) {
            *temperature = (float)temp->valuedouble;
            *published = value;
            valid = true;
        }
    }
    cJSON_Delete(root);
    return valid;
}

static bool fetch_weather(float *temperature, time_t *published)
{
    char *body = malloc(WEATHER_BODY_MAX + 1);
    if (!body) return false;
    esp_http_client_config_t cfg = {
        .url = "https://www.nmc.cn/f/rest/real/wdcXE",
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    bool valid = false;
    if (client && esp_http_client_open(client, 0) == ESP_OK &&
        esp_http_client_fetch_headers(client) >= 0 &&
        esp_http_client_get_status_code(client) == 200) {
        int used = 0, count;
        while (used < WEATHER_BODY_MAX &&
               (count = esp_http_client_read(client, body + used, WEATHER_BODY_MAX - used)) > 0)
            used += count;
        body[used] = '\0';
        if (used < WEATHER_BODY_MAX && esp_http_client_is_complete_data_received(client))
            valid = parse_weather(body, time(NULL), temperature, published);
    }
    if (client) esp_http_client_cleanup(client);
    free(body);
    return valid;
}

static void weather_task(void *arg)
{
    (void)arg;
    time_t published = 0, next_fetch = 0;
    bool visible = false;
    for (;;) {
        time_t now = time(NULL);
        if (now >= 1704067200 && now >= next_fetch && wifi_get_state() == EPW_WIFI_CONNECTED) {
            float temperature;
            if (fetch_weather(&temperature, &published)) {
                struct tm date;
                localtime_r(&published, &date);
                char text[96];
                snprintf(text, sizeof(text), "萧山 %.1f°C\n中央气象台 %02d:%02d",
                         temperature, date.tm_hour, date.tm_min);
                bsp_display_lock(-1);
                ui_update_weather(text);
                bsp_display_unlock();
                visible = true;
                next_fetch = time(NULL) + 900;
                ESP_LOGI(TAG, "Xiaoshan %.1f C, observed %02d:%02d", temperature, date.tm_hour, date.tm_min);
            } else {
                next_fetch = time(NULL) + 60;
                ESP_LOGW(TAG, "weather unavailable; retry in 60s");
            }
        }
        if (visible && (time(NULL) - published > WEATHER_MAX_AGE || time(NULL) < published - 300)) {
            bsp_display_lock(-1);
            ui_update_weather("萧山天气\n暂不可用");
            bsp_display_unlock();
            visible = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void weather_init(void)
{
    if (xTaskCreate(weather_task, "epw_weather", 6144, NULL, 3, NULL) != pdPASS)
        ESP_LOGE(TAG, "weather task creation failed");
}
