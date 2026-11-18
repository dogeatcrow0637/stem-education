#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VOICE_BARGE_SAMPLES 512

/* 由现有语音任务串行调用；采集/播放均为 16kHz。 */
bool voice_barge_begin(void);
bool voice_barge_feed(const int16_t *stereo, const int16_t *reference, size_t samples);
const int16_t *voice_barge_preroll(size_t *bytes);
void voice_barge_close(void);
