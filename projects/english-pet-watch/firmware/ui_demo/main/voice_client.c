/* 静音语音链路：宠物页左滑启动，退出/熄屏停止；只上传 Opus，回复显示为文字。 */
#include "voice_client.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_websocket_client.h"
#include "esp_opus_enc.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "bsp/esp-bsp.h"
#include "audio.h"
#include "ui.h"
#include "wifi.h"
#include "ws_message.h"

static const char *TAG = "epw-voice";
#define VOICE_SAMPLE_RATE 16000
#define VOICE_MAX_PCM     1920
#define VOICE_MAX_OPUS    2048
/* esp_audio_codec 的编码栈建议约 40KB，放 PSRAM 避免挤占内部 DMA 内存。 */
#define VOICE_TASK_STACK  (40 * 1024)

static esp_websocket_client_handle_t s_ws;
static esp_codec_dev_handle_t s_mic;
static void *s_enc;
static epw_ws_message_t *s_message;
static atomic_bool s_requested;
static atomic_bool s_ready;
static atomic_uint s_epoch;
static unsigned s_session_epoch;
static atomic_bool s_connected;
static atomic_bool s_hello;
static atomic_bool s_listening;
static atomic_bool s_reply_active;
static atomic_bool s_have_reply;
static atomic_uint s_progress_tick;
static unsigned s_tts_packets, s_tts_bytes;
static char s_session_id[96];

static bool session_current(void)
{
    return atomic_load(&s_requested) && atomic_load(&s_epoch) == s_session_epoch;
}

bool voice_client_is_active(void)
{
    return atomic_load(&s_requested);
}

bool voice_client_set_active(bool active)
{
    if (!active) {
        atomic_store(&s_requested, false);
        atomic_fetch_add(&s_epoch, 1);
        return false;
    }
    if (!atomic_load(&s_ready)) {
        return false;
    }
    if (!atomic_load(&s_requested)) {
        atomic_fetch_add(&s_epoch, 1);
        atomic_store(&s_requested, true);
    }
    return true;
}

static bool send_json(const char *json)
{
    if (s_ws == NULL || !atomic_load(&s_connected) || !session_current()) {
        return false;
    }
    int len = (int)strlen(json);
    return esp_websocket_client_send_text(s_ws, json, len, pdMS_TO_TICKS(500)) == len;
}

static void pet_set(const char *emotion, const char *text)
{
    if (session_current() && bsp_display_lock(200) == ESP_OK) {
        if (session_current()) {
            ui_activity();
            ui_pet_set_state_by_name(emotion, text);
        }
        bsp_display_unlock();
    }
}

static void send_listen_start(void)
{
    if (!atomic_load(&s_hello) || !session_current()) {
        return;
    }
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return;
    }
    bool ok = cJSON_AddStringToObject(json, "type", "listen") != NULL &&
              cJSON_AddStringToObject(json, "state", "start") != NULL &&
              cJSON_AddStringToObject(json, "mode", "auto") != NULL &&
              cJSON_AddStringToObject(json, "session_id", s_session_id) != NULL;
    char *text = ok ? cJSON_PrintUnformatted(json) : NULL;
    if (text != NULL && send_json(text)) {
        atomic_store(&s_listening, true);
        pet_set("listening", atomic_load(&s_have_reply) ? NULL : "我在听，你说吧。");
        ESP_LOGI(TAG, "listen start; output volume=0");
    } else {
        pet_set("sad", "语音请求失败，请重试。");
        voice_client_set_active(false);
    }
    cJSON_free(text);
    cJSON_Delete(json);
}

static const char *json_string(const cJSON *json, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static void handle_text(const char *data, size_t len)
{
    cJSON *json = cJSON_ParseWithLengthOpts(data, len + 1, NULL, true);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return;
    }
    const char *type = json_string(json, "type");
    const char *text = json_string(json, "text");
    if (strcmp(type, "hello") == 0) {
        const char *id = json_string(json, "session_id");
        if (id[0] != '\0' && strlen(id) < sizeof(s_session_id)) {
            snprintf(s_session_id, sizeof(s_session_id), "%s", id);
            atomic_store(&s_hello, true);
            atomic_store(&s_progress_tick, xTaskGetTickCount());
            send_listen_start();
            ESP_LOGI(TAG, "server hello accepted");
        } else {
            pet_set("sad", "语音握手失败，请重试。");
            voice_client_set_active(false);
        }
        cJSON_Delete(json);
        return;
    }
    const char *id = json_string(json, "session_id");
    if (!atomic_load(&s_hello) || (id[0] && strcmp(id, s_session_id))) {
        cJSON_Delete(json);
        return;
    }
    atomic_store(&s_progress_tick, xTaskGetTickCount());
    if (strcmp(type, "stt") == 0 && text[0] != '\0') {
        atomic_store(&s_reply_active, true);
        atomic_store(&s_listening, false);
        pet_set("thinking", text);
        ESP_LOGI(TAG, "ASR text received");
    } else if (strcmp(type, "llm") == 0) {
        const char *emotion = json_string(json, "emotion");
        pet_set(emotion[0] ? emotion : "speaking", text[0] ? text : NULL);
    } else if (strcmp(type, "tts") == 0) {
        const char *state = json_string(json, "state");
        if (strcmp(state, "start") == 0) {
            atomic_store(&s_listening, false);
            atomic_store(&s_reply_active, true);
            s_tts_packets = s_tts_bytes = 0;
        } else if (strcmp(state, "stop") == 0) {
            atomic_store(&s_reply_active, false);
            ESP_LOGI(TAG, "TTS received: %u packets, %u bytes; output volume=0",
                     s_tts_packets, s_tts_bytes);
            send_listen_start();
        } else if (strcmp(state, "sentence_start") == 0 && text[0] != '\0') {
            atomic_store(&s_have_reply, true);
            pet_set("speaking", text);
        }
    }
    cJSON_Delete(json);
}

static void ws_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    if (event_id == WEBSOCKET_EVENT_DISCONNECTED || event_id == WEBSOCKET_EVENT_CLOSED) {
        atomic_store(&s_connected, false);
        atomic_store(&s_hello, false);
        atomic_store(&s_listening, false);
        atomic_store(&s_reply_active, false);
        if (s_message != NULL) s_message->collecting = false;
        s_session_id[0] = '\0';
        pet_set("thinking", "连接语音…");
        return;
    }
    if (!session_current()) {
        return;
    }
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        atomic_store(&s_connected, true);
        atomic_store(&s_hello, false);
        atomic_store(&s_listening, false);
        atomic_store(&s_reply_active, false);
        atomic_store(&s_have_reply, false);
        s_session_id[0] = '\0';
        send_json("{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\","
                  "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,"
                  "\"channels\":1,\"frame_duration\":60}}");
    } else if (event_id == WEBSOCKET_EVENT_DATA) {
        esp_websocket_event_data_t *data = event_data;
        if (data == NULL || data->data_len < 0 || data->payload_len < 0 || data->payload_offset < 0) {
            return;
        }
        if (s_message && epw_ws_message_feed(s_message, data->op_code, data->fin,
                                (size_t)data->payload_offset, (size_t)data->payload_len,
                                data->data_ptr, (size_t)data->data_len)) {
            if (s_message->opcode == 1) handle_text(s_message->data, s_message->used);
            else if (s_message->opcode == 2 && atomic_load(&s_hello)) {
                s_tts_packets++;
                s_tts_bytes += s_message->used;
                atomic_store(&s_progress_tick, xTaskGetTickCount());
            }
        }
        /* 静音模式直接丢弃 TTS 二进制数据，不分配解码器和播放缓冲。 */
    }
}

static bool open_audio(void)
{
    if (!audio_init()) {
        return false;
    }
    if (s_mic == NULL) {
        s_mic = bsp_audio_codec_microphone_init();
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 1, .sample_rate = VOICE_SAMPLE_RATE,
    };
    if (s_mic == NULL || esp_codec_dev_open(s_mic, &fs) != ESP_CODEC_DEV_OK) {
        return false;
    }
    esp_opus_enc_config_t cfg = {
        .sample_rate = VOICE_SAMPLE_RATE, .channel = 1, .bits_per_sample = 16,
        .bitrate = ESP_OPUS_BITRATE_AUTO,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_VOIP,
        .complexity = 0,
    };
    bool ok = esp_opus_enc_open(&cfg, sizeof(cfg), &s_enc) == ESP_AUDIO_ERR_OK;
    if (ok) {
        ESP_LOGI(TAG, "microphone active (16 kHz mono)");
    }
    return ok;
}

static bool ws_start(void)
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return false;
    }
    char device_id[18];
    snprintf(device_id, sizeof(device_id), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char headers[512];
    int n = snprintf(headers, sizeof(headers),
                     "Authorization: Bearer %s\r\nProtocol-Version: 1\r\n"
                     "Device-Id: %s\r\nClient-Id: %s\r\n",
                     CONFIG_EPW_VOICE_TOKEN, device_id, device_id);
    if (n < 0 || n >= sizeof(headers) || strpbrk(CONFIG_EPW_VOICE_TOKEN, "\r\n") != NULL) {
        return false;
    }
    esp_websocket_client_config_t cfg = {
        .uri = CONFIG_EPW_VOICE_WS_URL, .headers = headers,
        .buffer_size = 2048, .task_stack = 6144,
        .reconnect_timeout_ms = 5000, .network_timeout_ms = 2000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    s_ws = esp_websocket_client_init(&cfg);
    if (s_ws == NULL || esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL) != ESP_OK) {
        return false;
    }
    return esp_websocket_client_start(s_ws) == ESP_OK;
}

static void close_audio(void)
{
    if (s_mic != NULL) {
        esp_codec_dev_close(s_mic);
    }
    if (s_enc != NULL) {
        esp_opus_enc_close(s_enc);
        s_enc = NULL;
    }
}

static void close_session(void)
{
    atomic_store(&s_listening, false);
    atomic_store(&s_hello, false);
    atomic_store(&s_reply_active, false);
    atomic_store(&s_have_reply, false);
    close_audio();
    if (s_ws != NULL) {
        if (esp_websocket_client_is_connected(s_ws)) {
            const char *stop = "{\"type\":\"abort\"}";
            esp_websocket_client_send_text(s_ws, stop, (int)strlen(stop), pdMS_TO_TICKS(200));
        }
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
    }
    atomic_store(&s_connected, false);
    free(s_message);
    s_message = NULL;
    ESP_LOGI(TAG, "voice session stopped; microphone closed");
}

static bool voice_wait_expired(uint32_t now, uint32_t hello_since)
{
    if (!atomic_load(&s_hello)) return (uint32_t)(now - hello_since) >= pdMS_TO_TICKS(20000);
    return atomic_load(&s_reply_active) &&
           (uint32_t)(now - atomic_load(&s_progress_tick)) >= pdMS_TO_TICKS(45000);
}

static void voice_task(void *arg)
{
    (void)arg;
    for (;;) {
        while (!atomic_load(&s_requested) || wifi_get_state() != EPW_WIFI_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        s_session_epoch = atomic_load(&s_epoch);
        s_message = heap_caps_calloc(1, sizeof(*s_message), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint8_t *pcm = NULL;
        uint8_t *opus = NULL;
        int in_size = 0, out_size = 0;
        uint32_t hello_since = xTaskGetTickCount();
        unsigned read_errors = 0;
        bool stack_logged = false;
        bool ok = s_message != NULL && session_current() && ws_start();
        if (!ok) {
            pet_set("sad", "语音连接失败，请重试。");
            if (session_current()) {
                voice_client_set_active(false);
            }
        }
        while (ok && session_current() && wifi_get_state() == EPW_WIFI_CONNECTED) {
            uint32_t now = xTaskGetTickCount();
            if (voice_wait_expired(now, hello_since)) {
                pet_set("sad", "语音响应超时，请左滑重试。");
                voice_client_set_active(false);
                break;
            }
            if (atomic_load(&s_hello)) hello_since = now;
            if (!atomic_load(&s_connected) || !atomic_load(&s_hello) || !atomic_load(&s_listening)) {
                if (!atomic_load(&s_connected) && s_enc != NULL) {
                    close_audio();
                    free(pcm);
                    pcm = NULL;
                    opus = NULL;
                }
                /* 回复期间排空输入 DMA，避免下一轮把上一轮缓存误送给 ASR。 */
                if (s_enc != NULL && atomic_load(&s_connected) && atomic_load(&s_hello)) {
                    if (esp_codec_dev_read(s_mic, pcm, in_size) != ESP_CODEC_DEV_OK)
                        vTaskDelay(pdMS_TO_TICKS(20));
                } else vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (s_enc == NULL) {
                pcm = heap_caps_malloc(VOICE_MAX_PCM + VOICE_MAX_OPUS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                opus = pcm != NULL ? pcm + VOICE_MAX_PCM : NULL;
                if (pcm == NULL || !open_audio() ||
                    esp_opus_enc_get_frame_size(s_enc, &in_size, &out_size) != ESP_AUDIO_ERR_OK ||
                    in_size <= 0 || in_size > VOICE_MAX_PCM || out_size <= 0 || out_size > VOICE_MAX_OPUS) {
                    pet_set("sad", "录音启动失败，请重试。");
                    if (session_current()) {
                        voice_client_set_active(false);
                    }
                    break;
                }
            }
            int ret = esp_codec_dev_read(s_mic, pcm, in_size);
            if (ret != ESP_CODEC_DEV_OK) {
                if (++read_errors >= 8) {
                    pet_set("sad", "麦克风读取失败，请重试。");
                    voice_client_set_active(false);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            read_errors = 0;
            if (!session_current() || !atomic_load(&s_listening)) {
                continue;
            }
            esp_audio_enc_in_frame_t in = {.buffer = pcm, .len = (uint32_t)in_size};
            esp_audio_enc_out_frame_t out = {.buffer = opus, .len = (uint32_t)out_size};
            if (esp_opus_enc_process(s_enc, &in, &out) == ESP_AUDIO_ERR_OK &&
                out.encoded_bytes > 0 && session_current() && atomic_load(&s_listening)) {
                if (!stack_logged) {
                    ESP_LOGI(TAG, "Opus encoding active; stack free min=%u bytes",
                             (unsigned)uxTaskGetStackHighWaterMark(NULL));
                    stack_logged = true;
                }
                esp_websocket_client_send_bin(s_ws, (const char *)opus, (int)out.encoded_bytes,
                                              pdMS_TO_TICKS(500));
            }
        }
        if (session_current() && wifi_get_state() != EPW_WIFI_CONNECTED) {
            pet_set("thinking", "等待网络…");
        }
        close_session();
        free(pcm);
    }
}

void voice_client_init(void)
{
    if (atomic_load(&s_ready) || CONFIG_EPW_VOICE_WS_URL[0] == '\0') {
        return;
    }
    atomic_store(&s_ready, xTaskCreateWithCaps(voice_task, "epw_voice", VOICE_TASK_STACK, NULL, 4, NULL,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS);
    if (!atomic_load(&s_ready)) {
        ESP_LOGE(TAG, "voice task create failed");
    }
}
