#ifndef APPLE_AUDIO_H
#define APPLE_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void apple_audio_init(void);
void apple_audio_speaker_click(uint64_t cycle);
void apple_audio_sync(uint64_t cycle);
void apple_audio_render_frame(uint64_t start_cycle, uint64_t end_cycle);

#ifdef __cplusplus
}
#endif

#endif /* APPLE_AUDIO_H */
