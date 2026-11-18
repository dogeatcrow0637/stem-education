/* 音频总线：录音固定 16 kHz 双声道后混为单声道；儿歌播放复用同一条 I2S。 */
#include "audio.h"

#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"

static const char *TAG = "epw-audio";

/* xiaozhi-server 设备端输入固定 16 kHz；输出也按 16 kHz 解码，避免重采样。 */
#define EPW_AUDIO_SAMPLE_RATE 16000
#define EPW_AUDIO_VOLUME      0
#define EPW_AUDIO_PLAY_VOLUME 0

static esp_codec_dev_handle_t s_spk;
static bool s_ready;
static bool s_play_open;
static int s_play_volume = EPW_AUDIO_PLAY_VOLUME;

/* esp_codec_dev 的 I2S 数据层只接受偶数声道；用 LEFT 掩码保持单声道数据流，
 * 硬件会把左槽数据复制到右槽，因此写入的仍是 16 kHz 单声道 PCM。 */
static esp_codec_dev_sample_info_t playback_fs(void)
{
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = EPW_AUDIO_SAMPLE_RATE,
    };
    return fs;
}

/* audio_init 由语音任务和儿歌播放任务共用，需要串行化。 */
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static portMUX_TYPE s_lock_mux = portMUX_INITIALIZER_UNLOCKED;

static SemaphoreHandle_t audio_lock(void)
{
    taskENTER_CRITICAL(&s_lock_mux);
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    }
    taskEXIT_CRITICAL(&s_lock_mux);
    return s_lock;
}

static bool audio_init_locked(void)
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

    esp_codec_dev_sample_info_t fs = playback_fs();
    if (esp_codec_dev_open(s_spk, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "speaker open failed during init");
        return false;
    }
    int ret = esp_codec_dev_set_out_vol(s_spk, EPW_AUDIO_VOLUME);
    /* BSP 会启用双工 I2S；静音模式关闭输出通道，录音仅打开输入。 */
    esp_codec_dev_close(s_spk);
    s_ready = ret == ESP_CODEC_DEV_OK;
    return s_ready;
}

bool audio_init(void)
{
    SemaphoreHandle_t lock = audio_lock();
    if (lock == NULL || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    bool ok = audio_init_locked();
    xSemaphoreGive(lock);
    return ok;
}

bool audio_playback_open(void)
{
    SemaphoreHandle_t lock = audio_lock();
    if (lock == NULL || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    bool ok = false;
    if (audio_init_locked()) {
        esp_codec_dev_sample_info_t fs = playback_fs();
        if (esp_codec_dev_open(s_spk, &fs) == ESP_CODEC_DEV_OK) {
            int err = esp_codec_dev_set_out_vol(s_spk, s_play_volume);
            if (err == ESP_CODEC_DEV_OK) {
                s_play_open = true;
                ok = true;
                ESP_LOGI(TAG, "playback open, volume=%d", s_play_volume);
            } else {
                ESP_LOGW(TAG, "speaker volume failed: %d", err);
                esp_codec_dev_close(s_spk);
            }
        } else {
            ESP_LOGW(TAG, "speaker open failed");
        }
    }
    xSemaphoreGive(lock);
    return ok;
}

int audio_playback_write(const void *pcm, size_t bytes)
{
    if (pcm == NULL || bytes == 0) {
        return 0;
    }
    SemaphoreHandle_t lock = audio_lock();
    if (lock == NULL || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return -1;
    }
    int ret = -1;
    if (s_play_open) {
        /* esp_codec_dev 成功返回 ESP_CODEC_DEV_OK(0)，不是写入字节数。 */
        int err = esp_codec_dev_write(s_spk, (void *)pcm, (int)bytes);
        if (err == ESP_CODEC_DEV_OK) {
            ret = (int)bytes;
        } else {
            ESP_LOGW(TAG, "speaker write failed: %d", err);
        }
    }
    xSemaphoreGive(lock);
    return ret;
}

void audio_playback_close(void)
{
    SemaphoreHandle_t lock = audio_lock();
    if (lock == NULL || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (s_play_open) {
        esp_codec_dev_close(s_spk);
        s_play_open = false;
    }
    xSemaphoreGive(lock);
}

void audio_set_playback_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    SemaphoreHandle_t lock = audio_lock();
    if (lock == NULL || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_play_volume = volume;
    if (s_play_open) {
        esp_codec_dev_set_out_vol(s_spk, s_play_volume);
    }
    xSemaphoreGive(lock);
}

int audio_get_playback_volume(void)
{
    return s_play_volume;
}

bool audio_playback_is_open(void)
{
    return s_play_open;
}
