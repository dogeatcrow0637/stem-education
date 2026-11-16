#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 I2S + ES8311 扬声器通路。重复调用安全；失败返回 false。 */
bool audio_init(void);

/** 设置输出音量 0..100（0 = 静音）。 */
void audio_set_volume(int percent);

/** 当前输出音量 0..100。 */
int audio_get_volume(void);

#ifdef __cplusplus
}
#endif
