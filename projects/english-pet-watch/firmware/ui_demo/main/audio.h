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
 * 儿歌播放与语音录音共用同一条 I2S，界面层负责保证二者不同时使用。
 */
bool audio_playback_open(void);

/** 写入 PCM。返回实际写入字节数，失败返回负数。 */
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
