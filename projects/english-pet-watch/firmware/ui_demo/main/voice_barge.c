#include "voice_barge.h"
#include <stdlib.h>
#include <string.h>
#include "esp_aec.h"
#include "esp_vad.h"
#include "esp_heap_caps.h"

/* 32ms AEC 块，保留 120ms 音频，避免触发打断前的开头被吞掉。 */
typedef struct {
    int16_t mic[VOICE_BARGE_SAMPLES];
    int16_t ref[VOICE_BARGE_SAMPLES];
    int16_t clean[VOICE_BARGE_SAMPLES];
    int16_t preroll[1920];
    size_t used;
    unsigned frames;
    unsigned quiet_frames;
    bool armed;
} barge_pcm_t;
static barge_pcm_t *s_pcm;
static aec_handle_t *s_aec;
static vad_handle_t s_vad;

void voice_barge_close(void)
{
    if (s_aec) aec_destroy(s_aec);
    if (s_vad) vad_destroy(s_vad);
    free(s_pcm);
    s_aec = NULL;
    s_vad = NULL;
    s_pcm = NULL;
}

bool voice_barge_begin(void)
{
    if (!s_pcm) {
        s_pcm = heap_caps_aligned_alloc(16, sizeof(*s_pcm), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        aec_config_t cfg = {
            .mic_num = 1, .ref_num = 1, .out_num = 1, .filter_length = 4,
            .sample_rate = 16000, .caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
            .mode = AEC_MODE_FD_LOW_COST, .nlp_level = AEC_NLP_LEVEL_AGGR,
        };
        s_aec = aec_create_from_config(&cfg);
        s_vad = vad_create_with_param(VAD_MODE_3, 16000, 30, 90, 90);
        if (!s_pcm || !s_aec || !s_vad || aec_get_chunksize(s_aec) != VOICE_BARGE_SAMPLES) {
            voice_barge_close();
            return false;
        }
    }
    memset(s_pcm, 0, sizeof(*s_pcm));
    vad_reset_trigger(s_vad);
    return true;
}

static void barge_keep(const int16_t *samples, size_t count)
{
    memmove(s_pcm->preroll, s_pcm->preroll + count, sizeof(s_pcm->preroll) - count * 2);
    memcpy(s_pcm->preroll + 1920 - count, samples, count * 2);
}

bool voice_barge_feed(const int16_t *stereo, const int16_t *reference, size_t samples)
{
    if (!s_pcm || !stereo || !reference) return false;
    bool detected = false;
    for (size_t i = 0; i < samples; i++) {
        size_t pos = s_pcm->used++;
        s_pcm->mic[pos] = ((int32_t)stereo[2*i] + stereo[2*i+1]) / 2;
        s_pcm->ref[pos] = reference[i];
        if (s_pcm->used != VOICE_BARGE_SAMPLES) continue;
        s_pcm->used = 0;
        aec_process(s_aec, s_pcm->mic, s_pcm->ref, s_pcm->clean);
        barge_keep(s_pcm->clean, VOICE_BARGE_SAMPLES);
        if (++s_pcm->frames <= 8) {
            vad_reset_trigger(s_vad);
            continue;
        }
        vad_state_t state = vad_process_with_trigger(s_vad, s_pcm->clean);
        int64_t energy = 0;
        for (int n = 0; n < 480; n++) energy += (int32_t)s_pcm->clean[n] * s_pcm->clean[n];
        /* 收敛后先确认约 96ms 安静，避免上一轮尾音直接打断新回答。 */
        if (!s_pcm->armed) {
            if (state != VAD_SPEECH && energy <= 480LL * 250 * 250) s_pcm->quiet_frames++;
            else s_pcm->quiet_frames = 0;
            if (s_pcm->quiet_frames >= 3) {
                s_pcm->armed = true;
                vad_reset_trigger(s_vad);
            }
            continue;
        }
        if (state == VAD_SPEECH && energy > 480LL * 250 * 250)
            detected = true;
    }
    return detected;
}

const int16_t *voice_barge_preroll(size_t *bytes)
{
    if (s_pcm && s_pcm->used) {
        size_t n = s_pcm->used;
        memset(s_pcm->mic + n, 0, (VOICE_BARGE_SAMPLES - n) * 2);
        memset(s_pcm->ref + n, 0, (VOICE_BARGE_SAMPLES - n) * 2);
        aec_process(s_aec, s_pcm->mic, s_pcm->ref, s_pcm->clean);
        barge_keep(s_pcm->clean, n);
        s_pcm->used = 0;
    }
    *bytes = s_pcm ? sizeof(s_pcm->preroll) : 0;
    return s_pcm ? s_pcm->preroll : NULL;
}
