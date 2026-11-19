#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 按需初始化 16 kHz 音频总线，扬声器保持静音且输出通道关闭。 */
bool audio_init(void);

/**
 * 打开扬声器输出（16 kHz / 单声道 / 16 bit）。
 * 总线固定双声道，内部复制单声道 PCM，支持语音播放时同步采集。
 * 儿歌与语音会话仍由界面层保证互斥。
 */
bool audio_playback_open(void);

/** 写入 PCM；音量为 0 时数字输出全为零。返回实际写入字节数，失败返回负数。 */
int audio_playback_write(const void *pcm, size_t bytes);

/** 关闭扬声器输出，释放功放。 */
void audio_playback_close(void);

/** 播放音量 0-100，立即生效并保存在内部状态里，下次打开沿用。 */
void audio_set_playback_volume(int volume);
int audio_get_playback_volume(void);
bool audio_playback_is_open(void);

#ifdef __cplusplus
}
#endif
