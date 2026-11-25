#ifndef C64_AUDIO_H
#define C64_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_OUT_HDMI = 0,
    AUDIO_OUT_I2S  = 1,
    AUDIO_OUT_PWM  = 2
} AudioOutputMode;

void    audioInit(int sampleRate);
void    audioReset(void);

void    audioSetOutputMode(uint8_t mode);
uint8_t audioGetOutputMode(void);

void    audioSetMute(bool mute);
void    audioSetVolume(uint8_t percent);
uint8_t audioGetVolume(void);

// Appelé par DigitalRenderer (SID_rp2350.cpp) pour envoyer un échantillon 16-bit
void    audioPushSidSample(int16_t left, int16_t right);

#ifdef __cplusplus
}
#endif

#endif /* C64_AUDIO_H */
