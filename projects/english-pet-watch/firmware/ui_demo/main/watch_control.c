/* 原生设备 MCP 工具；网络回调只入队，语音任务执行并串行交接扬声器。 */
#include "watch_control.h"
#include <ctype.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "nursery.h"
#include "watch_system.h"
#include "power.h"
#include "wifi.h"
#include "ui.h"

static const char *const s_tools[] = {
    "{\"name\":\"self.watch.status\",\"description\":\"读取手表真实状态：音量、亮度、电池、网络、计时器、儿歌和打卡。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"required\":[],\"additionalProperties\":false}}",
    "{\"name\":\"self.watch.volume\",\"description\":\"调整手表和Mimi的共用音量。value设绝对百分比；delta相对调整；二选一。静音value=0。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"value\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},\"delta\":{\"type\":\"integer\",\"minimum\":-100,\"maximum\":100}},\"required\":[],\"additionalProperties\":false}}",
    "{\"name\":\"self.watch.brightness\",\"description\":\"设置屏幕亮度。value绝对百分比或delta相对调整，二选一。睡眠模式下需先退出睡眠。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"value\":{\"type\":\"integer\",\"minimum\":10,\"maximum\":100},\"delta\":{\"type\":\"integer\",\"minimum\":-100,\"maximum\":100}},\"required\":[],\"additionalProperties\":false}}",
    "{\"name\":\"self.watch.search_songs\",\"description\":\"查手表3S儿歌曲库。query为英文歌名关键词，可将中文歌名翻译成英文后查找；返回真实song_id。播放前必须查找，不能猜ID。最多返回8首。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":95}},\"required\":[\"query\"],\"additionalProperties\":false}}",
    "{\"name\":\"self.watch.music\",\"description\":\"控制手表3S儿歌。play必须使用search_songs返回的song_id；weekly的slot为0到4。mode取list/single/shuffle。播放会在Mimi说完后结束聊天并进入播放器。进入聊天会停止儿歌，resume从上次那首歌开头重新播放，不能断点续播。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"play\",\"weekly\",\"pause\",\"resume\",\"stop\",\"next\",\"previous\",\"mode\"]},\"song_id\":{\"type\":\"string\",\"maxLength\":7},\"slot\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":4},\"mode\":{\"type\":\"string\",\"enum\":[\"list\",\"single\",\"shuffle\"]}},\"required\":[\"action\"],\"additionalProperties\":false}}",
    "{\"name\":\"self.watch.timer\",\"description\":\"倒计时控制。start需seconds（1到86399）；已有运行/暂停计时器需先cancel，不能悄悄覆盖。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"pause\",\"resume\",\"cancel\"]},\"seconds\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":86399}},\"required\":[\"action\"],\"additionalProperties\":false}}",
    "{\"name\":\"self.watch.screen\",\"description\":\"手电筒、睡眠、熄屏和自动熄屏时长。idle需seconds=0/15/30/60/120/300，0为常亮。其它操作说完后执行并退出聊天。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"flashlight_on\",\"flashlight_off\",\"sleep_on\",\"sleep_off\",\"off\",\"idle\"]},\"seconds\":{\"type\":\"integer\",\"enum\":[0,15,30,60,120,300]}},\"required\":[\"action\"],\"additionalProperties\":false}}",
    "{\"name\":\"self.watch.open\",\"description\":\"打开手表页面，说完后退出聊天。WiFi连接和密码在手表上操作。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"page\":{\"type\":\"string\",\"enum\":[\"home\",\"apps\",\"nursery\",\"wifi\",\"status\",\"brightness\",\"volume\",\"timer\",\"settings\"]}},\"required\":[\"page\"],\"additionalProperties\":false}}",
    "{\"name\":\"self.watch.checkin\",\"description\":\"儿歌每日打卡。start需slot：0早、1中、2晚；开始/继续会说完后进入播放器。redeem兑换已满足条件的愿望卡。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"pause\",\"resume\",\"end\",\"redeem\"]},\"slot\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":2}},\"required\":[\"action\"],\"additionalProperties\":false}}",
    "{\"name\":\"self.watch.shutdown\",\"description\":\"打开手表现有关机确认滑块；必须用户在手表上滑动确认，不会直接断电。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"required\":[],\"additionalProperties\":false}}",
};
static QueueHandle_t s_requests;
static SemaphoreHandle_t s_ui_done;
static bool s_initialized;
static atomic_uint s_connection;
static unsigned s_active_connection;
static int s_last_call_id;
static cJSON *s_pending;
static int s_previous_song = -1;
static bool s_previous_weekly;

void watch_control_remember_music(void)
{
    int index = nursery_current_index();
    if (index >= 0) {
        s_previous_song = index;
        s_previous_weekly = nursery_queue_is_weekly();
    }
}


static const char *string(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static cJSON *failure(const char *message)
{
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "error", message);
    return out;
}

static bool integer(const cJSON *value, int low, int high)
{
    return cJSON_IsNumber(value) && value->valuedouble >= low && value->valuedouble <= high &&
           value->valuedouble == (int)value->valuedouble;
}

static bool validate_args(const cJSON *schema, const cJSON *args)
{
    if (!cJSON_IsObject(args)) return false;
    const cJSON *required = cJSON_GetObjectItemCaseSensitive(schema, "required"), *item;
    cJSON_ArrayForEach(item, required) {
        if (!cJSON_GetObjectItemCaseSensitive(args, item->valuestring)) return false;
    }
    const cJSON *properties = cJSON_GetObjectItemCaseSensitive(schema, "properties");
    cJSON_ArrayForEach(item, args) {
        const cJSON *rule = cJSON_GetObjectItemCaseSensitive(properties, item->string);
        if (!rule) return false;
        const char *type = string(rule, "type");
        const cJSON *limit;
        if (!strcmp(type, "integer")) {
            if (!integer(item, -1000000, 1000000)) return false;
            limit = cJSON_GetObjectItemCaseSensitive(rule, "minimum");
            if (limit && item->valuedouble < limit->valuedouble) return false;
            limit = cJSON_GetObjectItemCaseSensitive(rule, "maximum");
            if (limit && item->valuedouble > limit->valuedouble) return false;
        } else if (!strcmp(type, "string")) {
            if (!cJSON_IsString(item)) return false;
            size_t len = strlen(item->valuestring);
            limit = cJSON_GetObjectItemCaseSensitive(rule, "minLength");
            if (limit && len < limit->valueint) return false;
            limit = cJSON_GetObjectItemCaseSensitive(rule, "maxLength");
            if (limit && len > limit->valueint) return false;
        } else return false;
        limit = cJSON_GetObjectItemCaseSensitive(rule, "enum");
        if (limit) {
            bool found = false;
            const cJSON *option;
            cJSON_ArrayForEach(option, limit) if (cJSON_Compare(option, item, true)) found = true;
            if (!found) return false;
        }
        /* cJSON 保留重复字段，拒绝歧义参数。 */
        for (const cJSON *other = item->next; other; other = other->next)
            if (!strcmp(item->string, other->string)) return false;
    }
    return true;
}

static void normalize(const char *text, char *out, size_t size)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p && n + 1 < size; p++) {
        if (*p >= 128 || isalnum(*p)) out[n++] = *p < 128 ? tolower(*p) : *p;
    }
    out[n] = '\0';
}

static int song_index(const char *id)
{
    if (!id[0]) return -1;
    for (int i = 0; i < nursery_song_count(); i++) {
        const epw_nursery_song_t *song = nursery_song(i);
        if (song && !strcmp(song->id, id)) return i;
    }
    return -1;
}

static cJSON *status(void)
{
    cJSON *out = cJSON_CreateObject();
    cJSON_AddNumberToObject(out, "volume", nursery_get_volume());
    cJSON_AddNumberToObject(out, "brightness", ui_watch_brightness());
    cJSON_AddNumberToObject(out, "battery_percent", power_battery_percent());
    cJSON_AddBoolToObject(out, "charging", power_is_charging());
    cJSON_AddStringToObject(out, "wifi", wifi_get_status_text());
    cJSON_AddNumberToObject(out, "idle_seconds", watch_idle_timeout_ms() / 1000);
    static const char *timers[] = {"idle", "running", "paused", "finished"};
    cJSON_AddStringToObject(out, "timer_state", timers[watch_timer_state()]);
    cJSON_AddNumberToObject(out, "timer_remaining_seconds", watch_timer_remaining());
    static const char *states[] = {"idle", "loading", "playing", "paused", "error"};
    cJSON_AddStringToObject(out, "music_state", states[nursery_get_state()]);
    cJSON_AddStringToObject(out, "music_error", nursery_status_text());
    const epw_nursery_song_t *song = nursery_song(nursery_current_index());
    if (song) {
        cJSON_AddStringToObject(out, "song_id", song->id);
        cJSON_AddStringToObject(out, "song_title", song->title);
    }
    static const char *modes[] = {"list", "single", "shuffle"};
    cJSON_AddStringToObject(out, "music_mode", modes[nursery_get_mode()]);
    cJSON *sessions = cJSON_AddArrayToObject(out, "checkins");
    for (int i = 0; i < 3; i++) {
        epw_nursery_session_t session;
        nursery_checkin_session(i, &session);
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "slot", i);
        cJSON_AddBoolToObject(entry, "active", session.active);
        cJSON_AddBoolToObject(entry, "paused", session.paused);
        cJSON_AddBoolToObject(entry, "done", session.done);
        cJSON_AddItemToArray(sessions, entry);
    }
    cJSON_AddNumberToObject(out, "weekly_stars", nursery_checkin_week_stars());
    cJSON_AddNumberToObject(out, "today_checkins", nursery_checkin_today_done());
    cJSON_AddBoolToObject(out, "wish_ready", nursery_checkin_wish_ready());
    return out;
}

static cJSON *execute(const char *name, const cJSON *args, bool deferred)
{
    const char *action = string(args, "action");
    if (!strcmp(name, "self.watch.status")) return status();
    if (!strcmp(name, "self.watch.volume") || !strcmp(name, "self.watch.brightness")) {
        bool volume = !strcmp(name, "self.watch.volume");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(args, "value");
        const cJSON *delta = cJSON_GetObjectItemCaseSensitive(args, "delta");
        if ((!value) == (!delta)) return failure("value和delta必须且只能提供一个");
        int target = value ? value->valueint :
            (volume ? nursery_get_volume() : ui_watch_brightness()) + delta->valueint;
        int low = volume ? 0 : 10;
        if (target < low) target = low;
        if (target > 100) target = 100;
        if (volume) {nursery_set_volume(target); nursery_save_volume();}
        else if (!ui_watch_set_brightness(target)) return failure("当前睡眠、手电筒或熄屏模式锁定亮度，请先关闭该模式");
        cJSON *out = cJSON_CreateObject();
        cJSON_AddNumberToObject(out, volume ? "volume" : "brightness", target);
        return out;
    }
    if (!strcmp(name, "self.watch.search_songs")) {
        if (!nursery_catalog_ready()) return failure("曲库尚未加载，请稍后重试");
        char query[96], title[EPW_NURSERY_TITLE_MAX];
        normalize(string(args, "query"), query, sizeof(query));
        if (!query[0]) return failure("请提供歌名关键词");
        cJSON *out = cJSON_CreateObject(), *songs = cJSON_AddArrayToObject(out, "songs");
        int count = 0;
        for (int i = 0; i < nursery_song_count(); i++) {
            const epw_nursery_song_t *song = nursery_song(i);
            if (!song) continue;
            normalize(song->title, title, sizeof(title));
            if (!strstr(title, query) && strcmp(song->id, string(args, "query"))) continue;
            if (count++ >= 8) continue;
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "song_id", song->id);
            cJSON_AddStringToObject(entry, "title", song->title);
            cJSON_AddNumberToObject(entry, "seconds", song->duration);
            cJSON_AddItemToArray(songs, entry);
        }
        cJSON_AddNumberToObject(out, "total_matches", count);
        return out;
    }
    if (!strcmp(name, "self.watch.timer")) {
        watch_timer_state_t state = watch_timer_state();
        const cJSON *seconds = cJSON_GetObjectItemCaseSensitive(args, "seconds");
        if (!strcmp(action, "start")) {
            if (!integer(seconds, 1, 86399) || !ui_watch_timer_start(seconds->valueint))
                return failure("需指定1到86399秒；已有计时器请先取消");
        } else if (!strcmp(action, "pause")) {
            if (state != WATCH_TIMER_RUNNING) return failure("没有正在运行的倒计时");
            watch_timer_pause();
        } else if (!strcmp(action, "resume")) {
            if (state != WATCH_TIMER_PAUSED) return failure("没有暂停的倒计时");
            watch_timer_start();
        } else if (!strcmp(action, "cancel")) watch_timer_reset();
        else return failure("未知倒计时操作");
        return status();
    }
    if (!strcmp(name, "self.watch.screen") && !strcmp(action, "idle")) {
        const cJSON *seconds = cJSON_GetObjectItemCaseSensitive(args, "seconds");
        if (!integer(seconds, 0, 300) || watch_set_idle_timeout_ms(seconds->valueint * 1000) != ESP_OK)
            return failure("熄屏秒数仅支持0/15/30/60/120/300");
        return status();
    }
    if (!strcmp(name, "self.watch.music")) {
        if (!strcmp(action, "mode")) {
            const char *mode = string(args, "mode");
            if (!mode[0]) return failure("请指定播放模式");
            nursery_set_mode(!strcmp(mode, "single") ? EPW_NURSERY_MODE_SINGLE :
                             !strcmp(mode, "shuffle") ? EPW_NURSERY_MODE_SHUFFLE : EPW_NURSERY_MODE_LIST);
            cJSON *out = cJSON_CreateObject(); cJSON_AddStringToObject(out, "mode", mode); return out;
        }
        if (!strcmp(action, "pause")) {nursery_pause(); return status();}
        if (!strcmp(action, "stop")) {s_previous_song = -1; nursery_stop(); return status();}
        int index = song_index(string(args, "song_id"));
        const cJSON *slot = cJSON_GetObjectItemCaseSensitive(args, "slot");
        if (!strcmp(action, "play") && index < 0) return failure("歌曲ID不存在，请先查询曲库");
        if (!strcmp(action, "weekly") && (!integer(slot, 0, 4) || nursery_weekly_index(slot->valueint) < 0))
            return failure("该本周曲库位置不可用");
        bool previous = !strcmp(action, "resume") || !strcmp(action, "next") || !strcmp(action, "previous");
        int remembered = nursery_current_index() >= 0 ? nursery_current_index() : s_previous_song;
        if (previous && (remembered < 0 || remembered >= nursery_song_count()))
            return failure("尚未选择歌曲，请先点歌");
        if (deferred) {
            ui_watch_open("nursery");
            if (!strcmp(action, "play")) nursery_play_index(index);
            else if (!strcmp(action, "weekly")) nursery_play_weekly(slot->valueint);
            else if (previous) {
                /* shortcut: 聊天释放流与解码器，继续会从该曲开头播放；支持音频 Range 后再恢复进度。 */
                int direction = !strcmp(action, "next") ? 1 : !strcmp(action, "previous") ? -1 : 0;
                int weekly_slot = -1, count = nursery_weekly_count();
                if (s_previous_weekly) {
                    for (int i = 0; i < count; i++) if (nursery_weekly_index(i) == remembered) weekly_slot = i;
                }
                if (weekly_slot >= 0) nursery_play_weekly((weekly_slot + direction + count) % count);
                else nursery_play_index((remembered + direction + nursery_song_count()) % nursery_song_count());
            }
            return status();
        }
    } else if (!strcmp(name, "self.watch.checkin")) {
        const cJSON *slot = cJSON_GetObjectItemCaseSensitive(args, "slot");
        if (!strcmp(action, "pause")) {nursery_checkin_pause(); return status();}
        if (!strcmp(action, "end")) {nursery_checkin_end(); return status();}
        if (!strcmp(action, "redeem")) {
            if (!nursery_checkin_redeem_wish()) return failure("尚未满足兑换条件或已经兑换");
            return status();
        }
        if (!nursery_checkin_enabled() || !nursery_catalog_ready()) return failure("打卡功能或曲库尚未就绪");
        bool active = false;
        for (int i = 0; i < 3; i++) {
            epw_nursery_session_t session;
            nursery_checkin_session(i, &session);
            active |= session.active;
        }
        if (!strcmp(action, "resume") && !active) return failure("没有正在进行的打卡");
        if (!strcmp(action, "start")) {
            if (active) return failure("请先结束当前这场打卡");
            if (!integer(slot, 0, 2)) return failure("请指定早0、中1、晚2的场次");
            epw_nursery_session_t session;
            nursery_checkin_session(slot->valueint, &session);
            if (session.done) return failure("该场次已完成");
        }
        if (deferred) {
            ui_watch_open("nursery");
            if (!strcmp(action, "start")) {
                if (!nursery_checkin_start(slot->valueint)) return failure("打卡启动失败，请查看打卡页");
            } else nursery_checkin_resume();
            return status();
        }
    } else if (deferred) {
        if (!strcmp(name, "self.watch.open")) ui_watch_open(string(args, "page"));
        else if (!strcmp(name, "self.watch.screen")) ui_watch_screen(action);
        else if (!strcmp(name, "self.watch.shutdown")) ui_power_menu_open();
        else return failure("未知设备功能");
        return status();
    }
    if (s_pending) return failure("已有页面或播放操作等待执行，请稍后再试");
    s_pending = cJSON_CreateObject();
    cJSON *copy = cJSON_Duplicate(args, true);
    if (!s_pending || !copy || !cJSON_AddStringToObject(s_pending, "name", name)) {
        cJSON_Delete(copy); cJSON_Delete(s_pending); s_pending = NULL;
        return failure("设备内存不足，请稍后重试");
    }
    cJSON_AddItemToObject(s_pending, "arguments", copy);
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "accepted", true);
    cJSON_AddStringToObject(out, "message", !strcmp(name, "self.watch.shutdown") ?
        "Mimi说完后打开关机滑块，需要用户在手表滑动确认，尚未关机" :
        "操作已排队，Mimi说完后退出聊天执行；播放需等待网络加载，尚未保证播放成功");
    return out;
}

typedef struct {
    const char *name;
    const cJSON *args;
    bool deferred;
    cJSON *result;
} control_ui_work_t;

static void control_ui_cb(void *arg)
{
    control_ui_work_t *work = arg;
    ui_activity();
    work->result = execute(work->name, work->args, work->deferred);
    xSemaphoreGive(s_ui_done);
}

static cJSON *execute_on_ui(const char *name, const cJSON *args, bool deferred)
{
    if (!s_ui_done || bsp_display_lock(1000) != ESP_OK) return failure("界面忙，请重试");
    control_ui_work_t work = {.name = name, .args = args, .deferred = deferred};
    /* 语音任务栈在 PSRAM；NVS 写入必须交给内部栈的 LVGL 任务，避免关闭缓存时崩溃。 */
    lv_result_t result = lv_async_call(control_ui_cb, &work);
    bsp_display_unlock();
    if (result != LV_RESULT_OK) return failure("界面操作排队失败");
    xSemaphoreTake(s_ui_done, portMAX_DELAY);
    return work.result;
}

bool watch_control_init(void)
{
    if (!s_requests) s_requests = xQueueCreate(6, sizeof(cJSON *));
    if (!s_ui_done) s_ui_done = xSemaphoreCreateBinary();
    return s_requests != NULL && s_ui_done != NULL;
}

void watch_control_new_connection(void)
{
    atomic_fetch_add(&s_connection, 1);
}

bool watch_control_receive(const cJSON *payload)
{
    if (!s_requests || !cJSON_IsObject(payload)) return false;
    /* 不保留初始化中的视觉 token，也不记录远端参数。 */
    cJSON *request = cJSON_CreateObject();
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(payload, "id");
    const char *method = string(payload, "method");
    if (!integer(id, 0, INT_MAX) || !method[0]) {cJSON_Delete(request); return false;}
    cJSON_AddNumberToObject(request, "_connection", atomic_load(&s_connection));
    cJSON_AddNumberToObject(request, "id", id->valueint);
    cJSON_AddStringToObject(request, "method", method);
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(payload, "params");
    if (strcmp(method, "initialize") && params) cJSON_AddItemToObject(request, "params", cJSON_Duplicate(params, true));
    if (!request || xQueueSend(s_requests, &request, 0) != pdTRUE) {cJSON_Delete(request); return false;}
    return true;
}

static cJSON *call_tool(const cJSON *request)
{
    int id = cJSON_GetObjectItemCaseSensitive(request, "id")->valueint;
    if (id <= s_last_call_id) return failure("重复或过期请求，未重复执行");
    s_last_call_id = id;
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(request, "params");
    const char *name = string(params, "name");
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(params, "arguments");
    for (unsigned i = 0; i < sizeof(s_tools) / sizeof(s_tools[0]); i++) {
        cJSON *tool = cJSON_Parse(s_tools[i]);
        bool match = !strcmp(string(tool, "name"), name);
        bool valid = match && validate_args(cJSON_GetObjectItemCaseSensitive(tool, "inputSchema"), args);
        cJSON_Delete(tool);
        if (!match) continue;
        if (!valid) return failure("工具参数不合法，请按设备工具定义调用");
        return execute_on_ui(name, args, false);
    }
    return failure("手表没有该功能");
}

void watch_control_poll(bool (*send)(const char *))
{
    cJSON *request;
    if (!s_requests || xQueueReceive(s_requests, &request, 0) != pdTRUE) return;
    unsigned connection = atomic_load(&s_connection);
    if (s_active_connection != connection) {
        watch_control_finish(false);
        s_last_call_id = 0;
        s_initialized = false;
        s_active_connection = connection;
    }
    if (cJSON_GetObjectItemCaseSensitive(request, "_connection")->valuedouble != connection) {
        cJSON_Delete(request);
        return;
    }
    const char *method = string(request, "method");
    cJSON *envelope = cJSON_CreateObject(), *payload = cJSON_AddObjectToObject(envelope, "payload");
    cJSON_AddStringToObject(envelope, "type", "mcp");
    cJSON_AddStringToObject(payload, "jsonrpc", "2.0");
    cJSON_AddItemToObject(payload, "id", cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(request, "id"), true));
    cJSON *result = NULL;
    if (!strcmp(method, "initialize")) {
        s_initialized = true;
        result = cJSON_Parse("{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"english-pet-watch\",\"version\":\"1.0\"}}");
    } else if (s_initialized && !strcmp(method, "tools/list")) {
        result = cJSON_CreateObject();
        cJSON *tools = cJSON_AddArrayToObject(result, "tools");
        const char *cursor = string(cJSON_GetObjectItemCaseSensitive(request, "params"), "cursor");
        /* 每页五项，避免挤占 WebSocket 任务和模型输入缓冲。 */
        int start = !strcmp(cursor, "5") ? 5 : 0;
        for (int i = start; i < start + 5 && i < sizeof(s_tools) / sizeof(s_tools[0]); i++)
            cJSON_AddItemToArray(tools, cJSON_Parse(s_tools[i]));
        if (!start) cJSON_AddStringToObject(result, "nextCursor", "5");
    } else if (s_initialized && !strcmp(method, "tools/call")) {
        cJSON *out = call_tool(request);
        result = cJSON_CreateObject();
        const char *error = string(out, "error");
        cJSON_AddBoolToObject(result, "isError", error[0] != '\0');
        if (error[0]) cJSON_AddStringToObject(result, "error", error);
        cJSON *content = cJSON_AddArrayToObject(result, "content"), *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "type", "text");
        char *text = cJSON_PrintUnformatted(out);
        cJSON_AddStringToObject(entry, "text", text ? text : "设备内存不足");
        free(text); cJSON_Delete(out); cJSON_AddItemToArray(content, entry);
    } else {
        cJSON *error = cJSON_AddObjectToObject(payload, "error");
        cJSON_AddNumberToObject(error, "code", -32601);
        cJSON_AddStringToObject(error, "message", "方法不可用或尚未初始化");
    }
    if (result) cJSON_AddItemToObject(payload, "result", result);
    char *raw = cJSON_PrintUnformatted(envelope);
    if (!raw || !send(raw)) {
        /* 未送达的排队操作不得在用户不知情时执行。 */
        watch_control_finish(false);
    }
    free(raw); cJSON_Delete(envelope); cJSON_Delete(request);
}

bool watch_control_pending(void)
{
    return s_pending != NULL;
}

void watch_control_finish(bool apply)
{
    if (!s_pending) return;
    cJSON *pending = s_pending;
    s_pending = NULL;
    if (apply) {
        cJSON *out = execute_on_ui(string(pending, "name"), cJSON_GetObjectItemCaseSensitive(pending, "arguments"), true);
        ESP_LOGI("epw-control", "voice action completed; error=%s", string(out, "error"));
        cJSON_Delete(out);
    }
    cJSON_Delete(pending);
}

void watch_control_reset(void)
{
    cJSON *request;
    while (s_requests && xQueueReceive(s_requests, &request, 0) == pdTRUE) cJSON_Delete(request);
    s_initialized = false;
    s_last_call_id = 0;
    watch_control_finish(false);
}
