/*
 * 3S 英语儿歌（family-hub / modules/nursery）手表端实现。
 *
 * 曲库与 MP3 都在家庭小站（内网/公网 HTTP），设备只拉取歌曲索引和歌词，
 * 音频按 HTTP 流解码成 16 kHz 单声道后写 ES8311，不把 1.2 GB 音频塞进 Flash。
 * 周曲库算法与网页版一致（每周六换一批，同一周所有设备结果相同）。
 */
#include "nursery.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "cJSON.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "wifi.h"

static const char *TAG = "epw-nursery";

#define NURSERY_BASE        "http://<曲库服务器>:8083/family/modules/nursery/"
#define NURSERY_CATALOG_URL NURSERY_BASE "data/songs.js"
#define NURSERY_LYRICS_URL  NURSERY_BASE "lyrics/"
#define NURSERY_AUDIO_URL   NURSERY_BASE "audio/"

#define NURSERY_NVS_NS      "nursery"
#define NURSERY_HTTP_MS     15000
#define NURSERY_CATALOG_MAX (72 * 1024)
#define NURSERY_LYRIC_MAX   (16 * 1024)
#define NURSERY_LYRIC_LINES 200
#define NURSERY_TARGET_RATE 16000
#define NURSERY_PCM_CAP     (16 * 1024)
#define NURSERY_MONO_CAP    (NURSERY_PCM_CAP / 2)
#define NURSERY_OUT_CHUNK   512

#define NURSERY_WEEK_SALT   "nursery-v1"
#define NURSERY_STARS_GOAL  5
#define NURSERY_SESSION_COUNT 3

static const uint8_t kSessionMinutes[NURSERY_SESSION_COUNT] = {40, 10, 40};
/* 早 + 晚都完成才有 1 颗星，与网页 config.js 的 starSlots 一致。 */
static const bool kStarSlot[NURSERY_SESSION_COUNT] = {true, false, true};

/* -------------------------------------------------------------------------- */
/* 数据                                                                        */
/* -------------------------------------------------------------------------- */

typedef struct {
    uint32_t start_ms;
    uint32_t end_ms;
    char text[EPW_NURSERY_LYRIC_TEXT];
} nursery_lyric_line_t;

typedef struct {
    uint8_t version;
    uint8_t volume;
    uint8_t mode;
} nursery_cfg_t;

typedef struct {
    uint8_t valid;
    uint8_t paused;
    uint8_t slot;
    uint8_t reserved;
    uint32_t elapsed_s;
    uint32_t duration_s;
    int32_t day;       /* YYYYMMDD */
    int16_t last_index;
    uint16_t reserved2;
} nursery_active_t;

static epw_nursery_song_t *s_songs;
static volatile bool s_catalog_ready;
static volatile bool s_catalog_loading;
static volatile int s_song_count;

static SemaphoreHandle_t s_data_lock;
static SemaphoreHandle_t s_lyric_lock;
static SemaphoreHandle_t s_nvs_lock;
static nvs_handle_t s_nvs;
static bool s_nvs_ok;

static nursery_cfg_t s_cfg = {.version = 1, .volume = 0, .mode = EPW_NURSERY_MODE_LIST};

/* 本周曲库（当前周） */
static char s_week_key[12];
static int s_week_slots[EPW_NURSERY_WEEKLY_MAX];
static int s_week_count;
static int16_t s_pool[EPW_NURSERY_MAX_SONGS];

/* 当前周手动换歌结果：week=id,id,... */
static char s_pl_week[12];
static char s_pl_ids[EPW_NURSERY_WEEKLY_MAX][8];
static int s_pl_count;

/* 歌词 */
static nursery_lyric_line_t *s_lyrics;
static int s_lyric_count;
static char s_lyric_song[8];

/* 打卡会话（UI 线程访问） */
static nursery_active_t s_act;
static uint32_t s_act_elapsed_ms;
static uint32_t s_act_saved_ms;
static uint32_t s_tick_ms;
static char s_status[64];

/* 播放任务 */
typedef struct {
    uint8_t cmd;
    int16_t arg;
} nursery_cmd_t;

enum {
    CMD_PLAY_INDEX = 1,
    CMD_PLAY_WEEKLY,
    CMD_PAUSE,
    CMD_RESUME,
    CMD_NEXT,
    CMD_PREV,
    CMD_STOP,
    CMD_SET_VOLUME,
};

enum {
    SONG_CONTINUE = 0,
    SONG_END,
    SONG_NEXT,
    SONG_PREV,
    SONG_STOP,
    SONG_REPLACE,
    SONG_ERROR,
};

static QueueHandle_t s_cmd_q;
static int16_t s_queue[EPW_NURSERY_MAX_SONGS];
static volatile int s_queue_len;
static volatile int s_queue_pos;
static volatile bool s_queue_weekly;
static volatile int s_state = EPW_NURSERY_IDLE;
static volatile int s_current = -1;
static volatile uint32_t s_elapsed_ms;
static volatile uint32_t s_duration_s;
static nursery_cmd_t s_pending_cmd;

/* 解码缓冲放 PSRAM，避免占用内部 RAM */
static uint8_t *s_mp3_in;
static uint8_t *s_pcm_out;
static int16_t *s_mono_buf;
static int16_t *s_pcm_chunk;
static esp_audio_simple_dec_handle_t s_dec;

/* -------------------------------------------------------------------------- */
/* 小工具                                                                      */
/* -------------------------------------------------------------------------- */

static void *ps_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) {
        p = malloc(size);
    }
    return p;
}

static void status_set(const char *text)
{
    strlcpy(s_status, text != NULL ? text : "", sizeof(s_status));
}

static bool time_valid(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    return tm_now.tm_year + 1900 >= 2024;
}

static int day_of(time_t t)
{
    struct tm tm_now;
    localtime_r(&t, &tm_now);
    return (tm_now.tm_year + 1900) * 10000 + (tm_now.tm_mon + 1) * 100 + tm_now.tm_mday;
}

static time_t week_start_of(time_t t)
{
    struct tm tm_now;
    localtime_r(&t, &tm_now);
    int diff = (tm_now.tm_wday - 6 + 7) % 7; /* 周六换曲库 */
    tm_now.tm_hour = 0;
    tm_now.tm_min = 0;
    tm_now.tm_sec = 0;
    tm_now.tm_mday -= diff;
    return mktime(&tm_now);
}

static void week_key_of(time_t t, char out[12])
{
    struct tm tm_now;
    time_t start = week_start_of(t);
    localtime_r(&start, &tm_now);
    snprintf(out, 12, "%04d-%02d-%02d", tm_now.tm_year + 1900,
             tm_now.tm_mon + 1, tm_now.tm_mday);
}

static uint32_t hash_string(const char *text)
{
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t mulberry32(uint32_t *state)
{
    *state += 0x6D2B79F5u;
    uint32_t t = *state;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}

static double mulberry_unit(uint32_t *state)
{
    return (double)mulberry32(state) / 4294967296.0;
}

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static const epw_nursery_song_t *song_at(int index)
{
    if (!s_catalog_ready || s_songs == NULL || index < 0 || index >= s_song_count) {
        return NULL;
    }
    return &s_songs[index];
}

static int song_index_by_id(const char *id)
{
    if (id == NULL || id[0] == '\0') {
        return -1;
    }
    if (!s_catalog_ready) {
        return -1;
    }
    for (int i = 0; i < s_song_count; i++) {
        if (strcmp(s_songs[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------- */
/* NVS                                                                         */
/* -------------------------------------------------------------------------- */

static bool nvs_take(void)
{
    return s_nvs_ok && s_nvs_lock != NULL &&
           xSemaphoreTake(s_nvs_lock, pdMS_TO_TICKS(500)) == pdTRUE;
}

static void nvs_give(void)
{
    if (s_nvs_lock != NULL) {
        xSemaphoreGive(s_nvs_lock);
    }
}

static void nvs_save_cfg(void)
{
    if (!nvs_take()) {
        return;
    }
    nvs_set_blob(s_nvs, "cfg", &s_cfg, sizeof(s_cfg));
    nvs_commit(s_nvs);
    nvs_give();
}

static void nvs_save_active(void)
{
    if (!nvs_take()) {
        return;
    }
    nursery_active_t out = s_act;
    out.elapsed_s = s_act_elapsed_ms / 1000;
    if (out.valid) {
        nvs_set_blob(s_nvs, "act", &out, sizeof(out));
    } else {
        nvs_erase_key(s_nvs, "act");
    }
    nvs_commit(s_nvs);
    nvs_give();
}

static void rec_key(int day, char out[16])
{
    snprintf(out, 16, "d%08d", day);
}

static bool nvs_load_record(int day, uint32_t ts[NURSERY_SESSION_COUNT])
{
    memset(ts, 0, sizeof(uint32_t) * NURSERY_SESSION_COUNT);
    char key[16];
    rec_key(day, key);
    if (!nvs_take()) {
        return false;
    }
    size_t size = sizeof(uint32_t) * NURSERY_SESSION_COUNT;
    esp_err_t err = nvs_get_blob(s_nvs, key, ts, &size);
    nvs_give();
    return err == ESP_OK && size == sizeof(uint32_t) * NURSERY_SESSION_COUNT;
}

static bool nvs_save_record(int day, const uint32_t ts[NURSERY_SESSION_COUNT])
{
    char key[16];
    rec_key(day, key);
    if (!nvs_take()) {
        return false;
    }
    nvs_set_blob(s_nvs, key, ts, sizeof(uint32_t) * NURSERY_SESSION_COUNT);
    nvs_commit(s_nvs);
    nvs_give();
    return true;
}

static void nvs_load_custom_playlist(void)
{
    s_pl_week[0] = '\0';
    s_pl_count = 0;
    if (!nvs_take()) {
        return;
    }
    char buf[128];
    size_t size = sizeof(buf);
    if (nvs_get_str(s_nvs, "pl", buf, &size) == ESP_OK) {
        char *eq = strchr(buf, '=');
        if (eq != NULL && (size_t)(eq - buf) == 10) {
            *eq = '\0';
            strlcpy(s_pl_week, buf, sizeof(s_pl_week));
            char *rest = eq + 1;
            char *save = NULL;
            for (char *id = strtok_r(rest, ",", &save);
                 id != NULL && s_pl_count < EPW_NURSERY_WEEKLY_MAX;
                 id = strtok_r(NULL, ",", &save)) {
                strlcpy(s_pl_ids[s_pl_count], id, sizeof(s_pl_ids[0]));
                s_pl_count++;
            }
        }
    }
    nvs_give();
}

static void nvs_save_custom_playlist(void)
{
    if (!nvs_take()) {
        return;
    }
    if (s_pl_count <= 0) {
        nvs_erase_key(s_nvs, "pl");
    } else {
        char buf[128];
        int used = snprintf(buf, sizeof(buf), "%s=", s_pl_week);
        for (int i = 0; i < s_pl_count && used < (int)sizeof(buf) - 4; i++) {
            used += snprintf(buf + used, sizeof(buf) - used, "%s%s",
                             i == 0 ? "" : ",", s_pl_ids[i]);
        }
        nvs_set_str(s_nvs, "pl", buf);
    }
    nvs_commit(s_nvs);
    nvs_give();
}

static bool nvs_wish_has(const char *week)
{
    if (!nvs_take()) {
        return false;
    }
    char buf[160];
    size_t size = sizeof(buf);
    bool found = false;
    if (nvs_get_str(s_nvs, "wish", buf, &size) == ESP_OK) {
        found = strstr(buf, week) != NULL;
    }
    nvs_give();
    return found;
}

static void nvs_wish_add(const char *week)
{
    if (!nvs_take()) {
        return;
    }
    char buf[160];
    size_t size = sizeof(buf);
    buf[0] = '\0';
    nvs_get_str(s_nvs, "wish", buf, &size);
    char next[160];
    if (buf[0] == '\0') {
        strlcpy(next, week, sizeof(next));
    } else {
        snprintf(next, sizeof(next), "%s,%s", buf, week);
    }
    nvs_set_str(s_nvs, "wish", next);
    nvs_commit(s_nvs);
    nvs_give();
}

/* -------------------------------------------------------------------------- */
/* 曲库                                                                        */
/* -------------------------------------------------------------------------- */

static bool json_field_str(const char *line, const char *key, char *out, size_t size)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(line, pattern);
    if (p == NULL) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (p == NULL) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;
    size_t n = 0;
    while (*p != '\0' && *p != '"' && n + 1 < size) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0;
}

static bool json_field_int(const char *line, const char *key, int *out)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(line, pattern);
    if (p == NULL) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (p == NULL) {
        return false;
    }
    *out = atoi(p + 1);
    return true;
}

static bool parse_song_line(const char *line, epw_nursery_song_t *song)
{
    int duration = 0;
    if (!json_field_str(line, "id", song->id, sizeof(song->id)) ||
        !json_field_str(line, "title", song->title, sizeof(song->title)) ||
        !json_field_str(line, "file", song->file, sizeof(song->file))) {
        return false;
    }
    json_field_int(line, "duration", &duration);
    song->duration = (uint16_t)clamp_int(duration, 0, 65535);
    return true;
}

static int http_get(const char *url, char *buf, size_t cap)
{
    if (buf == NULL || cap < 2) {
        return -1;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = NURSERY_HTTP_MS,
        .buffer_size = 2048,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return -1;
    }
    int total = -1;
    if (esp_http_client_open(client, 0) == ESP_OK &&
        esp_http_client_fetch_headers(client) >= 0 &&
        esp_http_client_get_status_code(client) == 200) {
        int used = 0;
        int count;
        while (used < (int)cap - 1 &&
               (count = esp_http_client_read(client, buf + used, (int)cap - 1 - used)) > 0) {
            used += count;
        }
        if (esp_http_client_is_complete_data_received(client)) {
            buf[used] = '\0';
            total = used;
        }
    }
    esp_http_client_cleanup(client);
    return total;
}

static bool catalog_fetch(void)
{
    char *body = ps_malloc(NURSERY_CATALOG_MAX);
    if (body == NULL) {
        ESP_LOGE(TAG, "catalog buffer allocation failed");
        return false;
    }
    int len = http_get(NURSERY_CATALOG_URL, body, NURSERY_CATALOG_MAX);
    if (len <= 0) {
        ESP_LOGW(TAG, "catalog download failed");
        free(body);
        return false;
    }

    epw_nursery_song_t *songs = ps_malloc(sizeof(epw_nursery_song_t) * EPW_NURSERY_MAX_SONGS);
    if (songs == NULL) {
        free(body);
        return false;
    }

    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(body, "\n", &save);
         line != NULL && count < EPW_NURSERY_MAX_SONGS;
         line = strtok_r(NULL, "\n", &save)) {
        char *trim = line;
        while (*trim == ' ' || *trim == '\t' || *trim == '\r') {
            trim++;
        }
        if (*trim != '{') {
            continue;
        }
        if (parse_song_line(trim, &songs[count])) {
            count++;
        }
    }
    free(body);

    if (count <= 0) {
        ESP_LOGW(TAG, "catalog empty");
        free(songs);
        return false;
    }

    for (int i = 0; i < count; i++) {
        s_songs[i] = songs[i]; /* 拷贝到固定容量数组，随后释放临时块 */
    }
    free(songs);
    s_song_count = count;
    s_catalog_ready = true;
    ESP_LOGI(TAG, "catalog ready: %d songs", count);
    return true;
}

static void catalog_task(void *arg)
{
    (void)arg;
    s_catalog_loading = true;
    while (!catalog_fetch()) {
        vTaskDelay(pdMS_TO_TICKS(8000));
    }
    s_catalog_loading = false;
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------- */
/* 周曲库                                                                      */
/* -------------------------------------------------------------------------- */

/* 调用前需持有 s_data_lock。 */
static bool weekly_apply_custom(const char *week)
{
    if (strcmp(s_pl_week, week) != 0 || s_pl_count <= 0) {
        return false;
    }
    int slots[EPW_NURSERY_WEEKLY_MAX];
    int count = 0;
    for (int i = 0; i < s_pl_count; i++) {
        int idx = song_index_by_id(s_pl_ids[i]);
        if (idx < 0) {
            return false;
        }
        bool dup = false;
        for (int j = 0; j < count; j++) {
            if (slots[j] == idx) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            slots[count++] = idx;
        }
    }
    if (count <= 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        s_week_slots[i] = slots[i];
    }
    s_week_count = count;
    return true;
}

/* 调用前需持有 s_data_lock；曲库必须已就绪。 */
static void weekly_generate(const char *week)
{
    int total = s_song_count;
    if (total <= 0) {
        s_week_count = 0;
        return;
    }
    char seed_text[32];
    snprintf(seed_text, sizeof(seed_text), "%s|%s", NURSERY_WEEK_SALT, week);
    uint32_t seed = hash_string(seed_text);
    int min = 4;
    int max = 5;
    if (max > total) {
        max = total;
    }
    if (min > max) {
        min = max;
    }
    int count = min + (int)(mulberry_unit(&seed) * (max - min + 1));
    if (count > EPW_NURSERY_WEEKLY_MAX) {
        count = EPW_NURSERY_WEEKLY_MAX;
    }
    for (int i = 0; i < total; i++) {
        s_pool[i] = (int16_t)i;
    }
    for (int i = total - 1; i > 0; i--) {
        int j = (int)(mulberry_unit(&seed) * (i + 1));
        if (j < 0 || j > i) {
            j = 0;
        }
        int16_t tmp = s_pool[i];
        s_pool[i] = s_pool[j];
        s_pool[j] = tmp;
    }
    for (int i = 0; i < count; i++) {
        s_week_slots[i] = s_pool[i];
    }
    s_week_count = count;
}

static void weekly_refresh_locked(void)
{
    char week[12];
    week_key_of(time(NULL), week);
    bool same = strcmp(week, s_week_key) == 0;
    if (same && s_week_count > 0) {
        return;
    }
    strlcpy(s_week_key, week, sizeof(s_week_key));
    s_week_count = 0;
    if (!s_catalog_ready) {
        return;
    }
    if (!weekly_apply_custom(week)) {
        weekly_generate(week);
    }
}

/* -------------------------------------------------------------------------- */
/* 歌词                                                                        */
/* -------------------------------------------------------------------------- */

static void lyrics_clear_locked(void)
{
    if (s_lyrics != NULL) {
        free(s_lyrics);
        s_lyrics = NULL;
    }
    s_lyric_count = 0;
    s_lyric_song[0] = '\0';
}

static void lyrics_load(const epw_nursery_song_t *song)
{
    if (song == NULL) {
        return;
    }
    if (s_lyric_lock != NULL &&
        xSemaphoreTake(s_lyric_lock, pdMS_TO_TICKS(300)) == pdTRUE) {
        bool cached = strcmp(s_lyric_song, song->id) == 0 && s_lyric_count >= 0;
        xSemaphoreGive(s_lyric_lock);
        if (cached) {
            return;
        }
    }

    char url[160];
    snprintf(url, sizeof(url), NURSERY_LYRICS_URL "%s.json", song->id);
    char *body = ps_malloc(NURSERY_LYRIC_MAX);
    if (body == NULL) {
        return;
    }
    int len = http_get(url, body, NURSERY_LYRIC_MAX);
    if (len <= 0) {
        free(body);
        body = NULL;
    }

    nursery_lyric_line_t *lines = NULL;
    int count = 0;
    cJSON *root = body != NULL ? cJSON_Parse(body) : NULL;
    free(body);
    cJSON *arr = root != NULL ? cJSON_GetObjectItemCaseSensitive(root, "lines") : NULL;
    if (cJSON_IsArray(arr)) {
        int total = cJSON_GetArraySize(arr);
        if (total > NURSERY_LYRIC_LINES) {
            total = NURSERY_LYRIC_LINES;
        }
        if (total > 0) {
            lines = ps_malloc(sizeof(nursery_lyric_line_t) * total);
        }
        if (lines != NULL) {
            cJSON *item;
            cJSON_ArrayForEach(item, arr) {
                if (count >= total) {
                    break;
                }
                cJSON *time = cJSON_GetObjectItemCaseSensitive(item, "time");
                cJSON *end = cJSON_GetObjectItemCaseSensitive(item, "end");
                cJSON *text = cJSON_GetObjectItemCaseSensitive(item, "text");
                if (!cJSON_IsNumber(time) || !cJSON_IsString(text)) {
                    continue;
                }
                const char *src = text->valuestring;
                while (*src == ' ' || *src == '\t') {
                    src++;
                }
                lines[count].start_ms = (uint32_t)(time->valuedouble * 1000.0);
                lines[count].end_ms = cJSON_IsNumber(end)
                                          ? (uint32_t)(end->valuedouble * 1000.0)
                                          : lines[count].start_ms;
                strlcpy(lines[count].text, src, sizeof(lines[count].text));
                count++;
            }
        }
    }
    cJSON_Delete(root);

    if (s_lyric_lock == NULL) {
        free(lines);
        return;
    }
    if (xSemaphoreTake(s_lyric_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        free(lines);
        return;
    }
    lyrics_clear_locked();
    s_lyrics = lines;
    s_lyric_count = count;
    strlcpy(s_lyric_song, song->id, sizeof(s_lyric_song));
    xSemaphoreGive(s_lyric_lock);
    ESP_LOGI(TAG, "lyrics %s: %d lines", song->id, count);
}

void nursery_get_lyric(char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }
    out[0] = '\0';
    if (s_lyric_lock == NULL ||
        xSemaphoreTake(s_lyric_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    if (s_lyrics == NULL || s_lyric_count <= 0) {
        xSemaphoreGive(s_lyric_lock);
        return;
    }
    uint32_t t = s_elapsed_ms;
    int found = -1;
    for (int i = 0; i < s_lyric_count; i++) {
        if (s_lyrics[i].start_ms <= t) {
            found = i;
        } else {
            break;
        }
    }
    if (found >= 0) {
        strlcpy(out, s_lyrics[found].text, size);
    }
    xSemaphoreGive(s_lyric_lock);
}

/* -------------------------------------------------------------------------- */
/* URL / 解码                                                                   */
/* -------------------------------------------------------------------------- */

static void url_encode(const char *in, char *out, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < size; p++) {
        unsigned char c = *p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else if (c == ' ') {
            out[o++] = '%';
            out[o++] = '2';
            out[o++] = '0';
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = '\0';
}

static void build_audio_url(const epw_nursery_song_t *song, char *out, size_t size)
{
    char encoded[EPW_NURSERY_FILE_MAX * 3];
    url_encode(song->file, encoded, sizeof(encoded));
    snprintf(out, size, NURSERY_AUDIO_URL "%s?v=1", encoded);
}

static int pump_commands(bool wait)
{
    nursery_cmd_t msg;
    TickType_t ticks = wait ? pdMS_TO_TICKS(100) : 0;
    while (xQueueReceive(s_cmd_q, &msg, ticks) == pdTRUE) {
        switch (msg.cmd) {
        case CMD_SET_VOLUME:
            audio_set_playback_volume(msg.arg);
            break;
        case CMD_PAUSE:
            if (s_state == EPW_NURSERY_PLAYING) {
                s_state = EPW_NURSERY_PAUSED;
            }
            break;
        case CMD_RESUME:
            if (s_state == EPW_NURSERY_PAUSED) {
                s_state = EPW_NURSERY_PLAYING;
            }
            break;
        case CMD_STOP:
            return SONG_STOP;
        case CMD_NEXT:
            return SONG_NEXT;
        case CMD_PREV:
            return SONG_PREV;
        case CMD_PLAY_INDEX:
        case CMD_PLAY_WEEKLY:
            s_pending_cmd = msg;
            return SONG_REPLACE;
        default:
            break;
        }
        ticks = 0; /* 第一轮之后只清空队列，不再等待 */
    }
    return SONG_CONTINUE;
}

static bool pcm_write(const int16_t *pcm, size_t samples)
{
    size_t bytes = samples * sizeof(int16_t);
    int ret = audio_playback_write(pcm, bytes);
    if (ret < 0 || (size_t)ret != bytes) {
        status_set("扬声器写入失败");
        return false;
    }
    s_elapsed_ms = (uint32_t)(s_elapsed_ms + (samples * 1000u) / NURSERY_TARGET_RATE);
    return true;
}

typedef struct {
    float step;
    float pos;
    int32_t prev;
    int channels;
} resampler_t;

static bool resample_chunk(const uint8_t *pcm, int bytes, const esp_audio_simple_dec_info_t *info,
                           resampler_t *rs)
{
    int channels = info->channel > 0 ? info->channel : 2;
    int frames = bytes / (2 * channels);
    if (frames <= 0) {
        return true;
    }
    int sample_rate = info->sample_rate > 0 ? (int)info->sample_rate : 44100;
    if (rs->channels != channels) {
        rs->channels = channels;
        rs->pos = 0.0f;
        rs->prev = 0;
        rs->step = (float)sample_rate / (float)NURSERY_TARGET_RATE;
        if (rs->step < 1.0f) {
            rs->step = 1.0f;
        }
    }

    const int16_t *in = (const int16_t *)pcm;
    if (channels == 1) {
        for (int i = 0; i < frames; i++) {
            s_mono_buf[i] = in[i];
        }
    } else {
        for (int i = 0; i < frames; i++) {
            int32_t l = in[i * channels];
            int32_t r = in[i * channels + 1];
            s_mono_buf[i] = (int16_t)((l + r) / 2);
        }
    }

    int out_n = 0;
    while (rs->pos < (float)(frames - 1)) {
        int i = (int)floorf(rs->pos);
        float frac = rs->pos - (float)i;
        int32_t a = i < 0 ? rs->prev : s_mono_buf[i];
        int32_t b = s_mono_buf[i + 1];
        s_pcm_chunk[out_n++] = (int16_t)(a + (int32_t)((b - a) * frac));
        if (out_n >= NURSERY_OUT_CHUNK) {
            if (!pcm_write(s_pcm_chunk, out_n)) {
                return false;
            }
            out_n = 0;
        }
        rs->pos += rs->step;
    }
    rs->pos -= (float)frames;
    rs->prev = s_mono_buf[frames - 1];
    if (out_n > 0 && !pcm_write(s_pcm_chunk, out_n)) {
        return false;
    }
    return true;
}

static int play_song(int index)
{
    const epw_nursery_song_t *song = song_at(index);
    if (song == NULL) {
        status_set("歌曲不存在");
        return SONG_ERROR;
    }

    s_current = index;
    s_duration_s = song->duration;
    s_elapsed_ms = 0;
    s_state = EPW_NURSERY_PLAYING;

    lyrics_load(song);

    char url[512];
    build_audio_url(song, url, sizeof(url));
    ESP_LOGI(TAG, "stream %s", url);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = NURSERY_HTTP_MS,
        .buffer_size = 4096,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        status_set("网络请求失败");
        return SONG_ERROR;
    }
    int result = SONG_ERROR;
    if (esp_http_client_open(client, 0) != ESP_OK ||
        esp_http_client_fetch_headers(client) < 0 ||
        esp_http_client_get_status_code(client) != 200) {
        status_set("歌曲下载失败");
        esp_http_client_cleanup(client);
        return SONG_ERROR;
    }

    if (s_dec == NULL) {
        esp_audio_simple_dec_cfg_t dcfg = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
            .dec_cfg = NULL,
            .cfg_size = 0,
            .use_frame_dec = false,
        };
        esp_audio_err_t derr = esp_audio_simple_dec_open(&dcfg, &s_dec);
        if (derr != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "mp3 decoder open failed: 0x%x", derr);
            status_set("MP3 解码器不可用");
            s_dec = NULL;
            esp_http_client_cleanup(client);
            return SONG_ERROR;
        }
    } else {
        esp_audio_simple_dec_reset(s_dec);
    }

    resampler_t rs = {0};
    esp_audio_simple_dec_info_t info = {0};
    bool resampler_ready = false;
    bool stream_done = false;
    bool failed = false;
    int idle_rounds = 0;

    while (!stream_done) {
        int ev = pump_commands(false);
        if (ev != SONG_CONTINUE) {
            result = ev;
            break;
        }
        while (s_state == EPW_NURSERY_PAUSED) {
            ev = pump_commands(true);
            if (ev != SONG_CONTINUE) {
                result = ev;
                failed = false;
                goto out;
            }
        }

        int read = esp_http_client_read(client, (char *)s_mp3_in, NURSERY_PCM_CAP / 4);
        if (read < 0) {
            status_set("音频流中断");
            failed = true;
            break;
        }
        bool eos = read == 0;
        if (eos && !esp_http_client_is_complete_data_received(client)) {
            status_set("音频流中断");
            failed = true;
            break;
        }
        esp_audio_simple_dec_raw_t raw = {
            .buffer = s_mp3_in,
            .len = (uint32_t)read,
            .eos = eos,
        };

        while ((int)raw.consumed < read || (eos && idle_rounds < 4)) {
            ev = pump_commands(false);
            if (ev != SONG_CONTINUE) {
                result = ev;
                goto out;
            }
            while (s_state == EPW_NURSERY_PAUSED) {
                ev = pump_commands(true);
                if (ev != SONG_CONTINUE) {
                    result = ev;
                    goto out;
                }
            }

            esp_audio_simple_dec_out_t out = {
                .buffer = s_pcm_out,
                .len = NURSERY_PCM_CAP,
            };
            esp_audio_err_t err = esp_audio_simple_dec_process(s_dec, &raw, &out);
            if (err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && out.needed_size > NURSERY_PCM_CAP) {
                status_set("解码缓冲不足");
                failed = true;
                goto out;
            }
            if (err != ESP_AUDIO_ERR_OK && err != ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                status_set("解码失败");
                failed = true;
                goto out;
            }
            if (!resampler_ready && out.decoded_size > 0) {
                if (esp_audio_simple_dec_get_info(s_dec, &info) == ESP_AUDIO_ERR_OK) {
                    resampler_ready = true;
                }
            }
            if (out.decoded_size > 0) {
                if (resampler_ready &&
                    !resample_chunk(s_pcm_out, (int)out.decoded_size, &info, &rs)) {
                    failed = true;
                    goto out;
                }
            }
            if (eos && out.decoded_size == 0) {
                idle_rounds++;
            }
        }

        if (eos) {
            stream_done = true;
        }
        if (s_state == EPW_NURSERY_PAUSED) {
            continue;
        }
    }

    if (!failed) {
        result = SONG_END;
        s_elapsed_ms = (uint32_t)song->duration * 1000u;
    }

out:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

/* -------------------------------------------------------------------------- */
/* 播放任务                                                                    */
/* -------------------------------------------------------------------------- */

static int queue_next(int pos, int step)
{
    int len = s_queue_len;
    if (len <= 0) {
        return -1;
    }
    if (s_cfg.mode == EPW_NURSERY_MODE_SHUFFLE && step > 0) {
        if (len == 1) {
            return pos;
        }
        int next = pos;
        for (int i = 0; i < 8 && next == pos; i++) {
            next = (int)(esp_random() % (uint32_t)len);
        }
        return next;
    }
    return (pos + step + len) % len;
}

static void playback_task(void *arg)
{
    (void)arg;
    bool has_pending = false;
    nursery_cmd_t msg = {0};

    for (;;) {
        if (!has_pending) {
            if (xQueueReceive(s_cmd_q, &msg, portMAX_DELAY) != pdTRUE) {
                continue;
            }
        }
        has_pending = false;

        if (msg.cmd == CMD_SET_VOLUME) {
            audio_set_playback_volume(msg.arg);
            continue;
        }
        if (msg.cmd == CMD_STOP) {
            s_state = EPW_NURSERY_IDLE;
            s_current = -1;
            s_elapsed_ms = 0;
            s_duration_s = 0;
            audio_playback_close();
            continue;
        }
        if (msg.cmd == CMD_PAUSE || msg.cmd == CMD_RESUME) {
            continue;
        }
        if (msg.cmd == CMD_NEXT || msg.cmd == CMD_PREV) {
            continue;
        }
        if (msg.cmd != CMD_PLAY_INDEX && msg.cmd != CMD_PLAY_WEEKLY) {
            continue;
        }

        s_state = EPW_NURSERY_LOADING;
        status_set("");
        if (msg.cmd == CMD_PLAY_INDEX) {
            if (!s_catalog_ready || msg.arg < 0 || msg.arg >= s_song_count) {
                s_state = EPW_NURSERY_ERROR;
                status_set("曲库未就绪");
                continue;
            }
            s_queue_len = s_song_count;
            for (int i = 0; i < s_song_count; i++) {
                s_queue[i] = (int16_t)i;
            }
            s_queue_pos = msg.arg;
            s_queue_weekly = false;
        } else {
            int slots[EPW_NURSERY_WEEKLY_MAX];
            int count = 0;
            if (s_data_lock != NULL &&
                xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(500)) == pdTRUE) {
                weekly_refresh_locked();
                count = s_week_count;
                for (int i = 0; i < count; i++) {
                    slots[i] = s_week_slots[i];
                }
                xSemaphoreGive(s_data_lock);
            }
            if (count <= 0) {
                s_state = EPW_NURSERY_ERROR;
                status_set("本周曲库为空");
                continue;
            }
            s_queue_len = count;
            for (int i = 0; i < count; i++) {
                s_queue[i] = (int16_t)slots[i];
            }
            s_queue_pos = clamp_int(msg.arg, 0, count - 1);
            s_queue_weekly = true;
        }

        if (!audio_playback_open()) {
            s_state = EPW_NURSERY_ERROR;
            status_set("扬声器不可用");
            continue;
        }

        int r;
        do {
            int index = s_queue[s_queue_pos];
            r = play_song(index);
            if (r == SONG_END) {
                if (s_cfg.mode == EPW_NURSERY_MODE_SINGLE) {
                    /* 单曲循环：位置不动 */
                } else {
                    s_queue_pos = queue_next(s_queue_pos, 1);
                }
            } else if (r == SONG_NEXT) {
                s_queue_pos = queue_next(s_queue_pos, 1);
            } else if (r == SONG_PREV) {
                s_queue_pos = queue_next(s_queue_pos, -1);
            }
            if (r == SONG_END || r == SONG_NEXT || r == SONG_PREV) {
                s_state = EPW_NURSERY_LOADING;
            }
        } while (r == SONG_END || r == SONG_NEXT || r == SONG_PREV);

        audio_playback_close();

        if (r == SONG_STOP || r == SONG_ERROR) {
            s_state = (r == SONG_STOP) ? EPW_NURSERY_IDLE : EPW_NURSERY_ERROR;
            s_current = -1;
            s_elapsed_ms = 0;
            s_duration_s = 0;
        } else if (r == SONG_REPLACE) {
            msg = s_pending_cmd;
            has_pending = true;
            s_state = EPW_NURSERY_LOADING;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 对外：初始化                                                                 */
/* -------------------------------------------------------------------------- */

void nursery_init(void)
{
    if (s_data_lock != NULL) {
        return;
    }
    s_data_lock = xSemaphoreCreateMutex();
    s_lyric_lock = xSemaphoreCreateMutex();
    s_nvs_lock = xSemaphoreCreateMutex();
    s_cmd_q = xQueueCreate(8, sizeof(nursery_cmd_t));

    /* esp_audio_simple_dec 是两层注册表：
     *   - 通用解码表（esp_audio_dec_register_default）里必须有 MP3；
     *   - 简单解码表（esp_audio_simple_dec_register_default）提供容器解析。
     * 只调其中任意一个都会出现「MP3 解码器不可用」。
     * 两个函数内部按 menuconfig 裁剪，实际只会链进 MP3 等已启用的解码器。 */
    esp_audio_err_t dec_err = esp_audio_dec_register_default();
    esp_audio_err_t sdec_err = esp_audio_simple_dec_register_default();
    if (dec_err != ESP_AUDIO_ERR_OK || sdec_err != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "decoder register: common=0x%x simple=0x%x",
                 dec_err, sdec_err);
    }
    esp_audio_err_t mp3_err =
        esp_audio_simple_check_audio_type(ESP_AUDIO_SIMPLE_DEC_TYPE_MP3);
    if (mp3_err != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "mp3 decoder not available: 0x%x", mp3_err);
    }

    s_songs = ps_malloc(sizeof(epw_nursery_song_t) * EPW_NURSERY_MAX_SONGS);
    s_mp3_in = ps_malloc(NURSERY_PCM_CAP / 4);
    s_pcm_out = ps_malloc(NURSERY_PCM_CAP);
    s_mono_buf = ps_malloc(NURSERY_MONO_CAP);
    s_pcm_chunk = ps_malloc(NURSERY_OUT_CHUNK * sizeof(int16_t));
    if (s_songs == NULL || s_mp3_in == NULL || s_pcm_out == NULL ||
        s_mono_buf == NULL || s_pcm_chunk == NULL || s_cmd_q == NULL) {
        ESP_LOGE(TAG, "buffer allocation failed");
        status_set("内存不足");
        return;
    }

    esp_err_t err = nvs_open(NURSERY_NVS_NS, NVS_READWRITE, &s_nvs);
    s_nvs_ok = err == ESP_OK;
    if (!s_nvs_ok) {
        ESP_LOGW(TAG, "NVS unavailable: %s", esp_err_to_name(err));
    }

    if (s_nvs_ok) {
        nursery_cfg_t cfg;
        size_t size = sizeof(cfg);
        if (nvs_get_blob(s_nvs, "cfg", &cfg, &size) == ESP_OK &&
            size == sizeof(cfg) && cfg.version == 1) {
            s_cfg = cfg;
        }
        s_cfg.volume = (uint8_t)clamp_int(s_cfg.volume, 0, 100);
        if (s_cfg.mode > EPW_NURSERY_MODE_SHUFFLE) {
            s_cfg.mode = EPW_NURSERY_MODE_LIST;
        }

        nursery_active_t act;
        size = sizeof(act);
        if (nvs_get_blob(s_nvs, "act", &act, &size) == ESP_OK &&
            size == sizeof(act) && act.valid && act.slot < NURSERY_SESSION_COUNT) {
            if (act.day == day_of(time(NULL)) && act.duration_s > 0 &&
                act.elapsed_s < act.duration_s) {
                act.paused = 1;
                s_act = act;
                s_act_elapsed_ms = act.elapsed_s * 1000u;
                s_tick_ms = (uint32_t)(esp_timer_get_time() / 1000);
            } else {
                nvs_erase_key(s_nvs, "act");
                nvs_commit(s_nvs);
            }
        }
        nvs_load_custom_playlist();
    }

    /* 每次开机从 0 开始，之后允许用户手动调整。 */
    audio_set_playback_volume(0);
    s_cfg.volume = (uint8_t)audio_get_playback_volume();

    if (xTaskCreate(catalog_task, "epw_catalog", 8192, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "catalog task creation failed");
    }
    if (xTaskCreatePinnedToCore(playback_task, "epw_play", 12288, NULL, 6, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "playback task creation failed");
    }
}

bool nursery_catalog_ready(void)
{
    return s_catalog_ready;
}

bool nursery_catalog_loading(void)
{
    return s_catalog_loading;
}

int nursery_song_count(void)
{
    return s_catalog_ready ? s_song_count : 0;
}

const epw_nursery_song_t *nursery_song(int index)
{
    return song_at(index);
}

/* -------------------------------------------------------------------------- */
/* 对外：周曲库                                                                 */
/* -------------------------------------------------------------------------- */

int nursery_weekly_count(void)
{
    if (s_data_lock == NULL) {
        return 0;
    }
    if (xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return s_week_count;
    }
    weekly_refresh_locked();
    int count = s_week_count;
    xSemaphoreGive(s_data_lock);
    return count;
}

int nursery_weekly_index(int slot)
{
    if (slot < 0 || slot >= nursery_weekly_count()) {
        return -1;
    }
    if (s_data_lock == NULL) {
        return -1;
    }
    if (xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return -1;
    }
    weekly_refresh_locked();
    int index = (slot < s_week_count) ? s_week_slots[slot] : -1;
    xSemaphoreGive(s_data_lock);
    return index;
}

bool nursery_weekly_contains(int catalog_index)
{
    int count = nursery_weekly_count();
    for (int i = 0; i < count; i++) {
        if (nursery_weekly_index(i) == catalog_index) {
            return true;
        }
    }
    return false;
}

bool nursery_weekly_replace(int slot)
{
    if (s_act.valid) {
        status_set("打卡中不能换歌");
        return false;
    }
    if (!s_catalog_ready || slot < 0) {
        return false;
    }
    if (s_data_lock == NULL ||
        xSemaphoreTake(s_data_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }
    weekly_refresh_locked();
    if (slot >= s_week_count || s_song_count <= s_week_count) {
        xSemaphoreGive(s_data_lock);
        return false;
    }
    int pick = -1;
    for (int tries = 0; tries < 32; tries++) {
        int candidate = (int)(esp_random() % (uint32_t)s_song_count);
        bool used = false;
        for (int i = 0; i < s_week_count; i++) {
            if (s_week_slots[i] == candidate) {
                used = true;
                break;
            }
        }
        if (!used) {
            pick = candidate;
            break;
        }
    }
    if (pick < 0) {
        xSemaphoreGive(s_data_lock);
        return false;
    }
    s_week_slots[slot] = pick;
    s_week_count = s_week_count > 0 ? s_week_count : EPW_NURSERY_WEEKLY_MAX;

    strlcpy(s_pl_week, s_week_key, sizeof(s_pl_week));
    s_pl_count = s_week_count;
    for (int i = 0; i < s_week_count; i++) {
        strlcpy(s_pl_ids[i], s_songs[s_week_slots[i]].id, sizeof(s_pl_ids[i]));
    }
    xSemaphoreGive(s_data_lock);
    nvs_save_custom_playlist();
    return true;
}

/* -------------------------------------------------------------------------- */
/* 对外：播放器                                                                 */
/* -------------------------------------------------------------------------- */

static void send_cmd(uint8_t cmd, int arg)
{
    if (s_cmd_q == NULL) {
        return;
    }
    nursery_cmd_t msg = {.cmd = cmd, .arg = (int16_t)clamp_int(arg, -32768, 32767)};
    xQueueSend(s_cmd_q, &msg, 0);
}

void nursery_play_index(int index)
{
    if (!s_catalog_ready || index < 0 || index >= s_song_count) {
        return;
    }
    s_state = EPW_NURSERY_LOADING;
    s_queue_weekly = false;
    s_current = index;
    send_cmd(CMD_PLAY_INDEX, index);
}

void nursery_play_weekly(int slot)
{
    int count = nursery_weekly_count();
    if (count <= 0) {
        return;
    }
    slot = clamp_int(slot, 0, count - 1);
    s_state = EPW_NURSERY_LOADING;
    s_queue_weekly = true;
    int index = nursery_weekly_index(slot);
    s_current = index;
    send_cmd(CMD_PLAY_WEEKLY, slot);
}

void nursery_toggle(void)
{
    if (s_state == EPW_NURSERY_PLAYING) {
        nursery_pause();
    } else if (s_state == EPW_NURSERY_PAUSED) {
        nursery_resume();
    } else if (s_queue_weekly && nursery_weekly_count() > 0) {
        nursery_play_weekly(0);
    } else if (s_current >= 0) {
        nursery_play_index(s_current);
    } else if (nursery_weekly_count() > 0) {
        nursery_play_weekly(0);
    }
}

void nursery_pause(void)
{
    if (s_state != EPW_NURSERY_PLAYING) {
        return;
    }
    s_state = EPW_NURSERY_PAUSED;
    send_cmd(CMD_PAUSE, 0);
}

void nursery_resume(void)
{
    if (s_state != EPW_NURSERY_PAUSED) {
        return;
    }
    s_state = EPW_NURSERY_PLAYING;
    send_cmd(CMD_RESUME, 0);
}

void nursery_next(void)
{
    if (s_queue_len > 0 && s_state != EPW_NURSERY_IDLE) {
        send_cmd(CMD_NEXT, 0);
    }
}

void nursery_prev(void)
{
    if (s_queue_len > 0 && s_state != EPW_NURSERY_IDLE) {
        send_cmd(CMD_PREV, 0);
    }
}

void nursery_stop(void)
{
    s_state = EPW_NURSERY_IDLE;
    s_current = -1;
    s_elapsed_ms = 0;
    s_duration_s = 0;
    send_cmd(CMD_STOP, 0);
}

void nursery_cycle_mode(void)
{
    s_cfg.mode = (s_cfg.mode + 1) % 3;
    nvs_save_cfg();
}

void nursery_set_mode(epw_nursery_mode_t mode)
{
    if (mode > EPW_NURSERY_MODE_SHUFFLE) {
        return;
    }
    s_cfg.mode = (uint8_t)mode;
    nvs_save_cfg();
}

epw_nursery_mode_t nursery_get_mode(void)
{
    return (epw_nursery_mode_t)s_cfg.mode;
}

void nursery_set_volume(int volume)
{
    audio_set_playback_volume(volume);
    s_cfg.volume = (uint8_t)audio_get_playback_volume();
}

int nursery_get_volume(void)
{
    return audio_get_playback_volume();
}

epw_nursery_state_t nursery_get_state(void)
{
    return (epw_nursery_state_t)s_state;
}

int nursery_current_index(void)
{
    return s_current;
}

bool nursery_queue_is_weekly(void)
{
    return s_queue_weekly;
}

uint32_t nursery_elapsed_ms(void)
{
    return s_elapsed_ms;
}

uint32_t nursery_duration_s(void)
{
    return s_duration_s;
}

const char *nursery_status_text(void)
{
    if (!s_catalog_ready) {
        if (wifi_get_state() != EPW_WIFI_CONNECTED) {
            return "网络未连接";
        }
        return s_catalog_loading ? "曲库加载中" : "曲库加载失败";
    }
    return s_status;
}

/* -------------------------------------------------------------------------- */
/* 对外：打卡                                                                   */
/* -------------------------------------------------------------------------- */

static int stars_for_week(time_t start)
{
    int stars = 0;
    for (int i = 0; i < 7; i++) {
        struct tm tm_day;
        localtime_r(&start, &tm_day);
        tm_day.tm_hour = 0;
        tm_day.tm_min = 0;
        tm_day.tm_sec = 0;
        tm_day.tm_mday += i;
        time_t day = mktime(&tm_day);
        uint32_t ts[NURSERY_SESSION_COUNT];
        if (!nvs_load_record(day_of(day), ts)) {
            continue;
        }
        bool star = true;
        for (int s = 0; s < NURSERY_SESSION_COUNT; s++) {
            if (kStarSlot[s] && ts[s] == 0) {
                star = false;
                break;
            }
        }
        if (star) {
            stars++;
        }
    }
    return stars;
}

bool nursery_checkin_enabled(void)
{
    return time_valid() && s_nvs_ok;
}

void nursery_checkin_session(int slot, epw_nursery_session_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (slot < 0 || slot >= NURSERY_SESSION_COUNT) {
        return;
    }
    out->target_minutes = kSessionMinutes[slot];
    if (s_act.valid && s_act.slot == slot) {
        out->elapsed_seconds = (int)(s_act_elapsed_ms / 1000);
        out->active = true;
        out->paused = s_act.paused;
        return;
    }
    uint32_t ts[NURSERY_SESSION_COUNT];
    if (nvs_load_record(day_of(time(NULL)), ts) && ts[slot] != 0) {
        struct tm tm_done;
        time_t done = (time_t)ts[slot];
        localtime_r(&done, &tm_done);
        out->done = true;
        out->completed_hour = tm_done.tm_hour;
        out->completed_minute = tm_done.tm_min;
    }
}

int nursery_checkin_today_done(void)
{
    uint32_t ts[NURSERY_SESSION_COUNT];
    if (!nvs_load_record(day_of(time(NULL)), ts)) {
        return 0;
    }
    int done = 0;
    for (int i = 0; i < NURSERY_SESSION_COUNT; i++) {
        if (ts[i] != 0) {
            done++;
        }
    }
    return done;
}

int nursery_checkin_week_stars(void)
{
    if (!time_valid()) {
        return 0;
    }
    return stars_for_week(week_start_of(time(NULL)));
}

int nursery_checkin_star_goal(void)
{
    return NURSERY_STARS_GOAL;
}

bool nursery_checkin_wish_ready(void)
{
    if (!time_valid()) {
        return false;
    }
    char week[12];
    week_key_of(time(NULL), week);
    return nursery_checkin_week_stars() >= NURSERY_STARS_GOAL &&
           !nvs_wish_has(week);
}

bool nursery_checkin_wish_redeemed(void)
{
    if (!time_valid()) {
        return false;
    }
    char week[12];
    week_key_of(time(NULL), week);
    return nvs_wish_has(week);
}

bool nursery_checkin_redeem_wish(void)
{
    if (!nursery_checkin_wish_ready()) {
        return false;
    }
    char week[12];
    week_key_of(time(NULL), week);
    nvs_wish_add(week);
    return true;
}

void nursery_checkin_history(int *out, int count)
{
    if (out == NULL || count <= 0) {
        return;
    }
    if (!time_valid()) {
        for (int i = 0; i < count; i++) {
            out[i] = 0;
        }
        return;
    }
    time_t start = week_start_of(time(NULL));
    for (int i = 0; i < count; i++) {
        struct tm tm_week;
        localtime_r(&start, &tm_week);
        tm_week.tm_mday -= 7 * i;
        time_t week = mktime(&tm_week);
        out[i] = stars_for_week(week);
    }
}

bool nursery_checkin_start(int slot)
{
    if (!nursery_checkin_enabled() || slot < 0 || slot >= NURSERY_SESSION_COUNT) {
        return false;
    }
    if (s_act.valid) {
        status_set("先结束当前这场");
        return false;
    }
    uint32_t ts[NURSERY_SESSION_COUNT];
    int today = day_of(time(NULL));
    if (nvs_load_record(today, ts) && ts[slot] != 0) {
        status_set("这场已经打卡");
        return false;
    }
    int count = nursery_weekly_count();
    if (count <= 0) {
        status_set(s_catalog_ready ? "本周曲库为空" : "曲库加载中");
        return false;
    }

    memset(&s_act, 0, sizeof(s_act));
    s_act.valid = 1;
    s_act.paused = 0;
    s_act.slot = (uint8_t)slot;
    s_act.duration_s = (uint32_t)kSessionMinutes[slot] * 60u;
    s_act.day = today;
    s_act.last_index = -1;
    s_act_elapsed_ms = 0;
    s_act_saved_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_tick_ms = s_act_saved_ms;
    nvs_save_active();
    status_set("");
    nursery_play_weekly(0);
    return true;
}

void nursery_checkin_pause(void)
{
    if (!s_act.valid || s_act.paused) {
        return;
    }
    s_act.paused = 1;
    nvs_save_active();
    nursery_pause();
}

void nursery_checkin_resume(void)
{
    if (!s_act.valid || !s_act.paused) {
        return;
    }
    s_act.paused = 0;
    nvs_save_active();
    if (nursery_get_state() == EPW_NURSERY_PAUSED) {
        nursery_resume();
        return;
    }
    int slot = 0;
    int count = nursery_weekly_count();
    int current = nursery_current_index();
    for (int i = 0; i < count; i++) {
        if (nursery_weekly_index(i) == current) {
            slot = i;
            break;
        }
    }
    nursery_play_weekly(slot);
}

void nursery_checkin_pause_toggle(void)
{
    if (!s_act.valid) {
        return;
    }
    if (s_act.paused) {
        nursery_checkin_resume();
    } else {
        nursery_checkin_pause();
    }
}

void nursery_checkin_end(void)
{
    if (!s_act.valid) {
        return;
    }
    memset(&s_act, 0, sizeof(s_act));
    s_act_elapsed_ms = 0;
    nvs_save_active();
    nursery_stop();
}

static void checkin_complete(void)
{
    int today = day_of(time(NULL));
    uint32_t ts[NURSERY_SESSION_COUNT];
    nvs_load_record(today, ts);
    if (ts[s_act.slot] == 0) {
        ts[s_act.slot] = (uint32_t)time(NULL);
        nvs_save_record(today, ts);
    }
    memset(&s_act, 0, sizeof(s_act));
    s_act_elapsed_ms = 0;
    nvs_save_active();
    nursery_stop();
    status_set("打卡完成");
}

void nursery_checkin_tick(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (!s_act.valid || s_act.paused || s_state != EPW_NURSERY_PLAYING) {
        s_tick_ms = now;
        return;
    }
    uint32_t delta = now - s_tick_ms;
    s_tick_ms = now;
    if (delta > 2000) {
        delta = 1000; /* 熄屏/切页造成的空档不能算进打卡 */
    }
    s_act_elapsed_ms += delta;
    if (s_act_elapsed_ms >= s_act.duration_s * 1000u) {
        checkin_complete();
        return;
    }
    if (now - s_act_saved_ms > 15000) {
        s_act_saved_ms = now;
        int current = nursery_current_index();
        if (current >= 0) {
            s_act.last_index = (int16_t)current;
        }
        s_act.elapsed_s = s_act_elapsed_ms / 1000;
        nvs_save_active();
    }
}
