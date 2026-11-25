/*
 * Sous-systeme Audio Oric Atmos (AY-3-8910 emu2149)
 * 3 backends compiles dans un seul firmware et commutables a chaud via le menu F4 :
 *   - AUDIO_OUT_HDMI (0) : HDMI Data Island L-PCM 48 kHz (Mini-HDMI PIO0)
 *   - AUDIO_OUT_I2S  (1) : I2S 48 kHz 32 bits (64*fS) sur PIO2 + DMA_IRQ_1 (GPIO 18/19/21)
 *   - AUDIO_OUT_PWM  (2) : PWM 96 kHz sur GPIO 26 (Slice 5 Ch A, PWM_IRQ_WRAP_0)
 */

#ifndef ORIC_AUDIO_H
#define ORIC_AUDIO_H

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

void    audioInit(int psgClock, int sampleRate);
void    audioReset(void);
void    audioStepCycles(uint8_t cycles);

void    audioSetOutputMode(uint8_t mode);
uint8_t audioGetOutputMode(void);

void    audioSetMute(bool mute);
void    audioSetVolume(uint8_t percent);
void    audioSetDcFilter(bool enable);

uint8_t audioReadPsg0(void);
void    audioWritePsg0(uint16_t addr, uint8_t val);

uint8_t audioSize(void);
void    audioSaveState(uint8_t *dst);
void    audioLoadState(const uint8_t *src);

#ifdef __cplusplus
}
#endif

#endif /* ORIC_AUDIO_H */
