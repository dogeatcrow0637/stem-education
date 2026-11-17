#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 按需初始化 16 kHz 音频总线，扬声器保持静音且输出通道关闭。 */
bool audio_init(void);

#ifdef __cplusplus
}
#endif
