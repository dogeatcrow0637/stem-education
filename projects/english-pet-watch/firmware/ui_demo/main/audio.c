/* 按需初始化音频总线。当前产品阶段只录音，ES8311 保持静音并关闭输出。 */
#include "audio.h"

#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "epw-audio";

/* xiaozhi-server 设备端输入固定 16 kHz；输出也按 16 kHz 解码，避免重采样。 */
#define EPW_AUDIO_SAMPLE_RATE 16000
#define EPW_AUDIO_VOLUME      0

static esp_codec_dev_handle_t s_spk;
static bool s_ready;

bool audio_init(void)
{
    if (s_ready) {
        return true;
    }

    const i2s_std_config_t i2s_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(EPW_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    esp_err_t err = bsp_audio_init(&i2s_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_audio_init failed: %s", esp_err_to_name(err));
        return false;
    }

    if (s_spk == NULL) {
        s_spk = bsp_audio_codec_speaker_init();
    }
    if (s_spk == NULL) {
        ESP_LOGW(TAG, "speaker codec init failed");
        return false;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 1, .sample_rate = EPW_AUDIO_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_spk, &fs) != ESP_CODEC_DEV_OK) {
        return false;
    }
    int ret = esp_codec_dev_set_out_vol(s_spk, EPW_AUDIO_VOLUME);
    /* BSP 会启用双工 I2S；静音模式关闭输出通道，录音仅打开输入。 */
    esp_codec_dev_close(s_spk);
    s_ready = ret == ESP_CODEC_DEV_OK;
    return s_ready;
}
