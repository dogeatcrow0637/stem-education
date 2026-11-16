#include "ota_client.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

#define OTA_HTTP_TIMEOUT_MS 20000
#define OTA_RESP_MAX        4096

static void get_device_id(char *buf, size_t len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void apply_server_time(const cJSON *server_time)
{
    if (!cJSON_IsObject(server_time)) {
        return;
    }

    const cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(server_time, "timestamp");
    const cJSON *timezone = cJSON_GetObjectItemCaseSensitive(server_time, "timezone_offset");
    if (!cJSON_IsNumber(timestamp)) {
        return;
    }

    int tz_minutes = cJSON_IsNumber(timezone) ? timezone->valueint : 0;
    struct timeval tv = {
        .tv_sec = (time_t)(timestamp->valuedouble / 1000.0) + (time_t)tz_minutes * 60,
        .tv_usec = 0,
    };
    settimeofday(&tv, NULL);
}

esp_err_t epw_ota_check(epw_ota_result_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    const esp_app_desc_t *app = esp_app_get_description();
    char device_id[18];
    get_device_id(device_id, sizeof(device_id));

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON *application = cJSON_AddObjectToObject(root, "application");
    cJSON *board = cJSON_AddObjectToObject(root, "board");
    if (application == NULL || board == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(application, "name", "english-pet-watch");
    cJSON_AddStringToObject(application, "version", app->version);
    cJSON_AddStringToObject(board, "type", CONFIG_EPW_DEVICE_MODEL);
    cJSON_AddStringToObject(board, "name", CONFIG_EPW_DEVICE_NAME);
    cJSON_AddStringToObject(root, "uuid", device_id);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {0};
    config.url = CONFIG_EPW_OTA_URL;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = OTA_HTTP_TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        cJSON_free(body);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "device-id", device_id);
    esp_http_client_set_header(client, "client-id", device_id);
    esp_http_client_set_header(client, "device-model", CONFIG_EPW_DEVICE_MODEL);
    esp_http_client_set_header(client, "device-version", app->version);
    char user_agent[64];
    snprintf(user_agent, sizeof(user_agent), "english-pet-watch/%s", app->version);
    esp_http_client_set_header(client, "User-Agent", user_agent);

    esp_err_t err = esp_http_client_open(client, strlen(body));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "连接 OTA 端点失败: %s", esp_err_to_name(err));
        goto cleanup;
    }

    int written = esp_http_client_write(client, body, strlen(body));
    if (written != (int)strlen(body)) {
        ESP_LOGE(TAG, "发送 OTA 请求失败");
        err = ESP_FAIL;
        goto cleanup;
    }

    if (esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGE(TAG, "读取 OTA 响应头失败");
        err = ESP_FAIL;
        goto cleanup;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "OTA 端点返回 HTTP %d", status);
        err = ESP_FAIL;
        goto cleanup;
    }

    char response[OTA_RESP_MAX];
    int total = 0;
    while (total < (int)sizeof(response) - 1) {
        int read = esp_http_client_read(client, response + total,
                                        sizeof(response) - 1 - total);
        if (read <= 0) {
            break;
        }
        total += read;
    }
    response[total] = '\0';

    cJSON *resp = cJSON_Parse(response);
    if (resp == NULL) {
        ESP_LOGE(TAG, "OTA 响应不是合法 JSON");
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const cJSON *firmware = cJSON_GetObjectItemCaseSensitive(resp, "firmware");
    if (cJSON_IsObject(firmware)) {
        const cJSON *version = cJSON_GetObjectItemCaseSensitive(firmware, "version");
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(firmware, "url");
        if (cJSON_IsString(version)) {
            snprintf(out->version, sizeof(out->version), "%s", version->valuestring);
        }
        if (cJSON_IsString(url) && url->valuestring[0] != '\0') {
            snprintf(out->url, sizeof(out->url), "%s", url->valuestring);
            if (out->version[0] != '\0' && strcmp(out->version, app->version) != 0) {
                out->update_available = true;
            }
        }
    }

    const cJSON *websocket = cJSON_GetObjectItemCaseSensitive(resp, "websocket");
    if (cJSON_IsObject(websocket)) {
        const cJSON *ws_url = cJSON_GetObjectItemCaseSensitive(websocket, "url");
        const cJSON *token = cJSON_GetObjectItemCaseSensitive(websocket, "token");
        if (cJSON_IsString(ws_url)) {
            snprintf(out->websocket_url, sizeof(out->websocket_url), "%s",
                     ws_url->valuestring);
        }
        if (cJSON_IsString(token) && token->valuestring[0] != '\0') {
            ESP_LOGI(TAG, "服务端要求 WebSocket 鉴权（token 已收到，V0.2 再接入）");
        }
    }

    apply_server_time(cJSON_GetObjectItemCaseSensitive(resp, "server_time"));
    cJSON_Delete(resp);

    if (out->update_available) {
        ESP_LOGW(TAG, "发现新固件: %s", out->version);
        ESP_LOGW(TAG, "下载地址: %s", out->url);
    } else {
        ESP_LOGI(TAG, "当前已是最新固件: %s", app->version);
    }

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    cJSON_free(body);
    return err;
}

esp_err_t epw_ota_apply(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGW(TAG, "开始下载固件: %s", url);

    esp_http_client_config_t http_config = {0};
    http_config.url = url;
    http_config.timeout_ms = OTA_HTTP_TIMEOUT_MS;
    http_config.keep_alive_enable = true;
    http_config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_https_ota_config_t ota_config = {0};
    ota_config.http_config = &http_config;

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA 初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    esp_app_desc_t new_app = {0};
    if (esp_https_ota_get_img_desc(handle, &new_app) == ESP_OK) {
        ESP_LOGI(TAG, "待升级固件版本: %s", new_app.version);
    }

    int last_percent = -1;
    while (true) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        int total = esp_https_ota_get_image_size(handle);
        int done = esp_https_ota_get_image_len_read(handle);
        if (total > 0) {
            int percent = done * 100 / total;
            if (percent >= last_percent + 10 || percent == 100) {
                ESP_LOGI(TAG, "OTA 进度: %d%% (%d/%d)", percent, done, total);
                last_percent = percent;
            }
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA 下载失败: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        return err;
    }

    if (!esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "OTA 数据不完整");
        esp_https_ota_abort(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA 写入分区失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA 写入完成，重启后生效");
    return ESP_OK;
}
