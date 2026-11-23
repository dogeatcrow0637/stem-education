#pragma once
#include <stdbool.h>
#include "cJSON.h"

bool watch_control_init(void);
/* 进入 Mimi 前由 UI 线程记住选择；现有播放器停止时会清空当前歌曲。 */
void watch_control_remember_music(void);
/* 回调接收；不读取 UI、不等待显示锁。 */
void watch_control_new_connection(void);
bool watch_control_receive(const cJSON *payload);
/* 以下仅由语音任务调用。 */
void watch_control_poll(bool (*send)(const char *));
bool watch_control_pending(void);
void watch_control_finish(bool execute);
void watch_control_reset(void);
