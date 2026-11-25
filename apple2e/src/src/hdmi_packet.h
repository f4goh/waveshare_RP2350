/*
 * Oric Atmos Emulator for Waveshare RP2350-PiZero
 * Encodeur HDMI Data Island (BCH ECC, TERC4, ACR, InfoFrames, L-PCM 48 kHz)
 * Architecture alignee sur pico-speccy (VID_HDMI_AUDIO)
 */

#ifndef ORIC_HDMI_PACKET_H
#define ORIC_HDMI_PACKET_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Paquet HDMI 1.3a / 1.4b :
 * - header[0..2] : 24 bits d'en-tete (HB0, HB1, HB2)
 * - header[3]    : 8 bits BCH ECC (polynome x^8 + x^2 + x + 1)
 * - subpacket[s][0..6] : 56 bits de donnees par sous-paquet (SB0..SB6)
 * - subpacket[s][7]    : 8 bits BCH ECC du sous-paquet s
 */
typedef struct {
    uint8_t header[4];
    uint8_t subpacket[4][8];
} HdmiPacket;

/*
 * Data Island encode en mots 32 bits PIO (2 symboles TMDS 10 bits par mot) :
 * - 1 mot  (2 px) : Leading Guard Band
 * - 16 mots (32 px) : Paquet encode en TERC4
 * - 1 mot  (2 px) : Trailing Guard Band
 * Total = 18 mots (36 pixels) par canal TMDS (ch0=Bleu, ch1=Vert, ch2=Rouge).
 */
#define HDMI_DATA_ISLAND_WORDS  18U

typedef struct {
    uint32_t ch0[HDMI_DATA_ISLAND_WORDS];
    uint32_t ch1[HDMI_DATA_ISLAND_WORDS];
    uint32_t ch2[HDMI_DATA_ISLAND_WORDS];
} HdmiDataIslandPio;

uint8_t hdmi_bch3(const uint8_t d[3]);
uint8_t hdmi_bch7(const uint8_t d[7]);
uint8_t hdmi_parity8(uint8_t v);

void hdmi_packet_set_null(HdmiPacket *pkt);
void hdmi_packet_set_acr(HdmiPacket *pkt, uint32_t n, uint32_t cts);
void hdmi_packet_set_avi_infoframe(HdmiPacket *pkt);
void hdmi_packet_set_vendor_infoframe(HdmiPacket *pkt);
void hdmi_packet_set_audio_infoframe(HdmiPacket *pkt);
void hdmi_packet_set_audio_samples(HdmiPacket *pkt,
                                   const int16_t left[4],
                                   const int16_t right[4],
                                   uint8_t *iecFrameIdx);

void hdmi_encode_data_island_pio(HdmiDataIslandPio *out,
                                 const HdmiPacket *pkt,
                                 bool vsync_active,
                                 bool hsync_active);

#ifdef __cplusplus
}
#endif

#endif /* ORIC_HDMI_PACKET_H */
