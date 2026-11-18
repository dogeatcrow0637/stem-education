#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 3S 英语儿歌（family-hub / modules/nursery）手表端适配。
 *
 * 网页版有两个视图：播放器 + 每日打卡。手表端保留核心功能：
 *   - 曲库 / 本周曲库 / 列表循环 / 单曲循环 / 随机播放
 *   - 播放、暂停、上一首、下一首、当前歌词
 *   - 早 / 中 / 晚三场打卡、本周星星、愿望卡兑换
 *   - 打卡记录、周曲库保存在 NVS，重启不丢
 *
 * 音频来自家庭小站：HTTP MP3 流 -> esp_audio_simple_dec -> ES8311。
 */

#define EPW_NURSERY_MAX_SONGS      512
#define EPW_NURSERY_WEEKLY_MAX     5
#define EPW_NURSERY_TITLE_MAX      96
#define EPW_NURSERY_FILE_MAX       112
#define EPW_NURSERY_LYRIC_MAX      120
#define EPW_NURSERY_LYRIC_TEXT     160

typedef enum {
    EPW_NURSERY_IDLE = 0,
    EPW_NURSERY_LOADING,
    EPW_NURSERY_PLAYING,
    EPW_NURSERY_PAUSED,
    EPW_NURSERY_ERROR,
} epw_nursery_state_t;

typedef enum {
    EPW_NURSERY_MODE_LIST = 0,
    EPW_NURSERY_MODE_SINGLE,
    EPW_NURSERY_MODE_SHUFFLE,
} epw_nursery_mode_t;

typedef struct {
    char id[8];
    char title[EPW_NURSERY_TITLE_MAX];
    char file[EPW_NURSERY_FILE_MAX];
    uint16_t duration; /* 秒 */
} epw_nursery_song_t;

typedef struct {
    int target_minutes;
    int elapsed_seconds;
    bool done;
    bool active;
    bool paused;
    int completed_hour;
    int completed_minute;
} epw_nursery_session_t;

/** 创建曲库 / 播放器 / 歌词任务，并恢复打卡记录。调用前无需持有 LVGL 锁。 */
void nursery_init(void);

/** 曲库是否已经从家庭小站拉取完成。 */
bool nursery_catalog_ready(void);
bool nursery_catalog_loading(void);
int nursery_song_count(void);
const epw_nursery_song_t *nursery_song(int index);

/* -------------------------------------------------------------------------- */
/* 周曲库                                                                      */
/* -------------------------------------------------------------------------- */

int nursery_weekly_count(void);
int nursery_weekly_index(int slot);              /* 返回曲库下标，失败 -1 */
bool nursery_weekly_contains(int catalog_index);
bool nursery_weekly_replace(int slot);           /* 长按换歌，打卡中拒绝 */

/* -------------------------------------------------------------------------- */
/* 播放器                                                                      */
/* -------------------------------------------------------------------------- */

void nursery_play_index(int index);              /* 全部曲库作为播放队列 */
void nursery_play_weekly(int slot);              /* 本周曲库作为播放队列 */
void nursery_toggle(void);
void nursery_pause(void);
void nursery_resume(void);
void nursery_next(void);
void nursery_prev(void);
void nursery_stop(void);

void nursery_cycle_mode(void);
void nursery_set_mode(epw_nursery_mode_t mode);
epw_nursery_mode_t nursery_get_mode(void);

void nursery_set_volume(int volume);             /* 0-100 */
int nursery_get_volume(void);
void nursery_save_volume(void);                 /* 调节结束时保存，避免逐帧写 NVS */

epw_nursery_state_t nursery_get_state(void);
int nursery_current_index(void);                 /* 曲库下标，-1 表示无 */
bool nursery_queue_is_weekly(void);
uint32_t nursery_elapsed_ms(void);
uint32_t nursery_duration_s(void);

/** 当前歌词行；没有歌词或还没到第一行时返回空串。 */
void nursery_get_lyric(char *out, size_t size);
/** 播放失败等短状态；正常返回空串。 */
const char *nursery_status_text(void);

/* -------------------------------------------------------------------------- */
/* 打卡                                                                        */
/* -------------------------------------------------------------------------- */

bool nursery_checkin_enabled(void);
void nursery_checkin_session(int slot, epw_nursery_session_t *out);
int nursery_checkin_today_done(void);
int nursery_checkin_week_stars(void);
int nursery_checkin_star_goal(void);
bool nursery_checkin_wish_ready(void);
bool nursery_checkin_wish_redeemed(void);
void nursery_checkin_history(int *out, int count); /* out[0]=本周 */

bool nursery_checkin_start(int slot);
void nursery_checkin_pause(void);
void nursery_checkin_resume(void);
void nursery_checkin_pause_toggle(void);
void nursery_checkin_end(void);
bool nursery_checkin_redeem_wish(void);

/** UI 每 1 秒调用一次：按实际播放时间累计打卡时长。 */
void nursery_checkin_tick(void);

#ifdef __cplusplus
}
#endif
