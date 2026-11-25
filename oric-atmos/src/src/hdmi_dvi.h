/*
 * Pilote DVI / HDMI PIO0 + DMA double-buffer pour Waveshare RP2350-PiZero (GPIO 32..39)
 * Supporte le basculement dynamique temps reel entre :
 * - Flux HDMI 1.4b avec Data Islands Audio L-PCM 48 kHz (quand la sortie audio HDMI est active)
 * - Flux DVI pur sans Data Islands (quand la sortie audio I2S ou PWM est active)
 */

#ifndef HDMI_DVI_H
#define HDMI_DVI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HDMI_LOGICAL_WIDTH   320U
#define HDMI_LOGICAL_HEIGHT  240U
#define HDMI_ACTIVE_WIDTH    640U
#define HDMI_ACTIVE_HEIGHT   480U

typedef void (*HdmiScanlineCb)(uint16_t y_logical, uint32_t *tmds_b, uint32_t *tmds_g, uint32_t *tmds_r);
typedef void (*HdmiEndFrameCb)(uint32_t frame_count);

void hdmi_dvi_init(HdmiScanlineCb scanline_cb, HdmiEndFrameCb endframe_cb);
void hdmi_dvi_launch_core1(void);

/* Activation/desactivation dynamique des Data Islands Audio HDMI */
void hdmi_dvi_set_audio_enabled(bool enable);
bool hdmi_dvi_is_audio_enabled(void);

/* File audio HDMI SPSC (4 echantillons stereo 16 bits a 48 kHz par paquet) */
void hdmi_dvi_push_audio_4samples(const int16_t left[4], const int16_t right[4]);
void hdmi_dvi_reset_audio_queue(void);

#ifdef __cplusplus
}
#endif

#endif /* HDMI_DVI_H */
