/*
 * apple_audio.c - 48 kHz Apple IIe Speaker Synthesizer -> Unified Audio (HDMI / I2S / PWM)
 */

#include "apple_audio.h"
#include "audio.h"
#include <string.h>

#define MAX_CLICKS_PER_FRAME 2048U
#define SAMPLES_PER_FRAME    800U /* 48000 Hz / 60 Hz */

static uint64_t s_clickCycles[MAX_CLICKS_PER_FRAME];
static uint32_t s_clickCount = 0;
static int32_t  s_speakerSign = 1;
static int32_t  s_dcOffset = 0;

void apple_audio_init(void)
{
    s_clickCount  = 0;
    s_speakerSign = 1;
    s_dcOffset    = 0;
}

void apple_audio_sync(uint64_t cycle)
{
    (void)cycle;
    s_clickCount = 0;
}

void apple_audio_speaker_click(uint64_t cycle)
{
    if (s_clickCount < MAX_CLICKS_PER_FRAME) {
        s_clickCycles[s_clickCount++] = cycle;
    }
}

void apple_audio_render_frame(uint64_t start_cycle, uint64_t end_cycle)
{
    uint64_t span = (end_cycle > start_cycle) ? (end_cycle - start_cycle) : 17030ULL;
    uint32_t c_idx = 0;
    uint64_t prev_cycle = start_cycle;

    for (uint32_t i = 0; i < SAMPLES_PER_FRAME; i++) {
        uint64_t sample_cycle = start_cycle + ((uint64_t)(i + 1u) * span) / SAMPLES_PER_FRAME;
        uint64_t dt = (sample_cycle > prev_cycle) ? (sample_cycle - prev_cycle) : 1ULL;
        uint64_t cur_t = prev_cycle;
        int64_t  accum = 0;

        while (c_idx < s_clickCount && s_clickCycles[c_idx] < sample_cycle) {
            uint64_t ct = s_clickCycles[c_idx];
            if (ct > cur_t) {
                accum += (int64_t)s_speakerSign * (int64_t)(ct - cur_t);
                cur_t = ct;
            }
            s_speakerSign = -s_speakerSign;
            c_idx++;
        }
        if (sample_cycle > cur_t) {
            accum += (int64_t)s_speakerSign * (int64_t)(sample_cycle - cur_t);
        }

        int32_t raw = (int32_t)((accum * 8192) / (int64_t)dt);
        /* Simple high-pass / DC-blocker so idle speaker decays smoothly to 0 */
        s_dcOffset += (raw - s_dcOffset) >> 9;
        int32_t out = raw - s_dcOffset;
        if (out > 32767) out = 32767;
        if (out < -32768) out = -32768;

        audioPushSidSample((int16_t)out, (int16_t)out);
        prev_cycle = sample_cycle;
    }

    while (c_idx < s_clickCount) {
        s_speakerSign = -s_speakerSign;
        c_idx++;
    }
    s_clickCount = 0;
}
