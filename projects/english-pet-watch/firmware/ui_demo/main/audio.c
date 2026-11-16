/*
 * 板载音频输出（ES8311 + I2S）。
 *
 * 只负责把扬声器通路打开并维护音量，不参与 TTS / 播放逻辑；
 * 上层以后接语音回答时直接复用这里初始化好的 codec 句柄即可。
 */
#include "audio.h"

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "epw-audio";

static esp_codec_dev_handle_t s_spk;
static int s_volume = 60;

bool audio_init(void)
{
    if (s_spk != NULL) {
        return true;
    }

    esp_err_t err = bsp_audio_init(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_audio_init failed: %s", esp_err_to_name(err));
        return false;
    }

    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        ESP_LOGW(TAG, "speaker codec init failed");
        return false;
    }

    audio_set_volume(s_volume);
    ESP_LOGI(TAG, "audio ready, volume=%d", s_volume);
    return true;
}

void audio_set_volume(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    s_volume = percent;
    if (s_spk != NULL) {
        esp_codec_dev_set_out_vol(s_spk, s_volume);
    }
}

int audio_get_volume(void)
{
    return s_volume;
}
