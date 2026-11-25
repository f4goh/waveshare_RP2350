/*
 * Oric Atmos Emulator for Waveshare RP2350-PiZero
 * Encodeur HDMI Data Island (BCH ECC, TERC4, ACR, InfoFrames, L-PCM 48 kHz)
 *
 * Alignement strict sur pico-speccy (src/drivers/hdmi/hdmi.c) :
 * - Table BCH(64,56)/(32,24) LSB-first exacte (0x00, 0xD9, 0xB5, 0x6C...) en SRAM.
 * - Fonctions BCH (3/7 octets), parite et transposition SWAR 4x8 100% inlinees en SRAM
 *   (__not_in_flash_func + always_inline) : zero appel en Flash XIP depuis Core 1.
 * - InfoFrames complets : AVI (VIC 1), Vendor Specific (OUI 0x000C03 HDMI Licensing),
 *   Audio InfoFrame (CT=0, CC=1, SF=0, SS=0 selon CEA-861-D / HDMI 1.4b).
 * - IEC 60958 Channel Status : Byte 0 = 0x04 (LPCM), Byte 3 = 0x02 (48 kHz),
 *   Byte 4 = 0x02 (16 bits).
 */

#include "hdmi_packet.h"
#include <string.h>

#if defined(PICO_BUILD) || defined(ARDUINO_ARCH_RP2040)
#include "pico/stdlib.h"
#define HDMI_PKT_FAST_FUNC(x) __not_in_flash_func(x)
#define HDMI_PKT_FAST_DATA    __not_in_flash("hdmi_audio")
#else
#define HDMI_PKT_FAST_FUNC(x) x
#define HDMI_PKT_FAST_DATA
#endif

#define HDMI_PAIR_WORD(s0, s1) (((uint32_t)(s0) & 0x3FFu) | (((uint32_t)(s1) & 0x3FFu) << 10))

/* Table TERC4 (4 bits -> 10 bits TMDS, HDMI 1.4b Table 5-4) en SRAM */
static const uint16_t HDMI_PKT_FAST_DATA s_terc4[16] = {
    0x29Cu, 0x263u, 0x2E4u, 0x2E2u,
    0x171u, 0x11Eu, 0x18Eu, 0x13Cu,
    0x2CCu, 0x139u, 0x19Cu, 0x2C6u,
    0x28Eu, 0x271u, 0x163u, 0x2C3u
};

/*
 * Table BCH(64,56)/(32,24) ECC HDMI 1.4b (polynome G(x) = 1 + x^6 + x^7 + x^8,
 * 0x83 LSB-first, 1 lookup par octet) placee en SRAM (identique a pico-speccy).
 */
static const uint8_t HDMI_PKT_FAST_DATA s_bchTable[256] = {
    0x00, 0xd9, 0xb5, 0x6c, 0x6d, 0xb4, 0xd8, 0x01, 0xda, 0x03, 0x6f, 0xb6, 0xb7, 0x6e, 0x02, 0xdb,
    0xb3, 0x6a, 0x06, 0xdf, 0xde, 0x07, 0x6b, 0xb2, 0x69, 0xb0, 0xdc, 0x05, 0x04, 0xdd, 0xb1, 0x68,
    0x61, 0xb8, 0xd4, 0x0d, 0x0c, 0xd5, 0xb9, 0x60, 0xbb, 0x62, 0x0e, 0xd7, 0xd6, 0x0f, 0x63, 0xba,
    0xd2, 0x0b, 0x67, 0xbe, 0xbf, 0x66, 0x0a, 0xd3, 0x08, 0xd1, 0xbd, 0x64, 0x65, 0xbc, 0xd0, 0x09,
    0xc2, 0x1b, 0x77, 0xae, 0xaf, 0x76, 0x1a, 0xc3, 0x18, 0xc1, 0xad, 0x74, 0x75, 0xac, 0xc0, 0x19,
    0x71, 0xa8, 0xc4, 0x1d, 0x1c, 0xc5, 0xa9, 0x70, 0xab, 0x72, 0x1e, 0xc7, 0xc6, 0x1f, 0x73, 0xaa,
    0xa3, 0x7a, 0x16, 0xcf, 0xce, 0x17, 0x7b, 0xa2, 0x79, 0xa0, 0xcc, 0x15, 0x14, 0xcd, 0xa1, 0x78,
    0x10, 0xc9, 0xa5, 0x7c, 0x7d, 0xa4, 0xc8, 0x11, 0xca, 0x13, 0x7f, 0xa6, 0xa7, 0x7e, 0x12, 0xcb,
    0x83, 0x5a, 0x36, 0xef, 0xee, 0x37, 0x5b, 0x82, 0x59, 0x80, 0xec, 0x35, 0x34, 0xed, 0x81, 0x58,
    0x30, 0xe9, 0x85, 0x5c, 0x5d, 0x84, 0xe8, 0x31, 0xea, 0x33, 0x5f, 0x86, 0x87, 0x5e, 0x32, 0xeb,
    0xe2, 0x3b, 0x57, 0x8e, 0x8f, 0x56, 0x3a, 0xe3, 0x38, 0xe1, 0x8d, 0x54, 0x55, 0x8c, 0xe0, 0x39,
    0x51, 0x88, 0xe4, 0x3d, 0x3c, 0xe5, 0x89, 0x50, 0x8b, 0x52, 0x3e, 0xe7, 0xe6, 0x3f, 0x53, 0x8a,
    0x41, 0x98, 0xf4, 0x2d, 0x2c, 0xf5, 0x99, 0x40, 0x9b, 0x42, 0x2e, 0xf7, 0xf6, 0x2f, 0x43, 0x9a,
    0xf2, 0x2b, 0x47, 0x9e, 0x9f, 0x46, 0x2a, 0xf3, 0x28, 0xf1, 0x9d, 0x44, 0x45, 0x9c, 0xf0, 0x29,
    0x20, 0xf9, 0x95, 0x4c, 0x4d, 0x94, 0xf8, 0x21, 0xfa, 0x23, 0x4f, 0x96, 0x97, 0x4e, 0x22, 0xfb,
    0x93, 0x4a, 0x26, 0xff, 0xfe, 0x27, 0x4b, 0x92, 0x49, 0x90, 0xfc, 0x25, 0x24, 0xfd, 0x91, 0x48
};

/* Table de parite 8 bits compressee (256 bits = 32 octets) en SRAM */
static const uint8_t HDMI_PKT_FAST_DATA s_parityTable[32] = {
    0x96, 0x69, 0x69, 0x96, 0x69, 0x96, 0x96, 0x69,
    0x69, 0x96, 0x96, 0x69, 0x96, 0x69, 0x69, 0x96,
    0x69, 0x96, 0x96, 0x69, 0x96, 0x69, 0x69, 0x96,
    0x96, 0x69, 0x69, 0x96, 0x69, 0x96, 0x96, 0x69
};

static inline __attribute__((always_inline)) uint8_t hdmi_bch3_fast(const uint8_t d[3])
{
    uint8_t v = s_bchTable[d[0]];
    v = s_bchTable[d[1] ^ v];
    v = s_bchTable[d[2] ^ v];
    return v;
}

static inline __attribute__((always_inline)) uint8_t hdmi_bch7_fast(const uint8_t d[7])
{
    uint8_t v = s_bchTable[d[0]];
    v = s_bchTable[d[1] ^ v];
    v = s_bchTable[d[2] ^ v];
    v = s_bchTable[d[3] ^ v];
    v = s_bchTable[d[4] ^ v];
    v = s_bchTable[d[5] ^ v];
    v = s_bchTable[d[6] ^ v];
    return v;
}

static inline __attribute__((always_inline)) uint8_t hdmi_parity8_fast(uint8_t v)
{
    return (uint8_t)((s_parityTable[v >> 3] >> (v & 7u)) & 1u);
}

uint8_t HDMI_PKT_FAST_FUNC(hdmi_bch3)(const uint8_t d[3])
{
    return hdmi_bch3_fast(d);
}

uint8_t HDMI_PKT_FAST_FUNC(hdmi_bch7)(const uint8_t d[7])
{
    return hdmi_bch7_fast(d);
}

uint8_t HDMI_PKT_FAST_FUNC(hdmi_parity8)(uint8_t v)
{
    return hdmi_parity8_fast(v);
}

/* Calcul du checksum InfoFrame CEA-861 (en-tete + payload complet) et des BCH */
static void hdmi_finalize_infoframe(HdmiPacket *pkt)
{
    int sum = (int)pkt->header[0] + (int)pkt->header[1] + (int)pkt->header[2];
    int n   = (int)pkt->header[2] + 1;
    for (int s = 0; s < 4; s++) {
        for (int i = 0; i < 7 && n > 0; i++, n--) {
            sum += (int)pkt->subpacket[s][i];
        }
    }
    pkt->subpacket[0][0] = (uint8_t)(-sum);
    pkt->header[3]       = hdmi_bch3_fast(pkt->header);
    for (int s = 0; s < 4; s++) {
        pkt->subpacket[s][7] = hdmi_bch7_fast(pkt->subpacket[s]);
    }
}

void hdmi_packet_set_null(HdmiPacket *pkt)
{
    memset(pkt, 0, sizeof(*pkt));
}

void hdmi_packet_set_acr(HdmiPacket *pkt, uint32_t n, uint32_t cts)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->header[0] = 0x01u;
    pkt->header[1] = 0x00u;
    pkt->header[2] = 0x00u;
    pkt->header[3] = hdmi_bch3_fast(pkt->header);

    uint8_t *sp0 = pkt->subpacket[0];
    sp0[0] = 0x00u;
    sp0[1] = (uint8_t)((cts >> 16) & 0x0Fu);
    sp0[2] = (uint8_t)((cts >> 8) & 0xFFu);
    sp0[3] = (uint8_t)(cts & 0xFFu);
    sp0[4] = (uint8_t)((n >> 16) & 0x0Fu);
    sp0[5] = (uint8_t)((n >> 8) & 0xFFu);
    sp0[6] = (uint8_t)(n & 0xFFu);
    sp0[7] = hdmi_bch7_fast(sp0);

    for (int s = 1; s < 4; s++) {
        memcpy(pkt->subpacket[s], sp0, 8u);
    }
}

void hdmi_packet_set_avi_infoframe(HdmiPacket *pkt)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->header[0] = 0x82u; /* AVI InfoFrame */
    pkt->header[1] = 0x02u; /* Version 2 */
    pkt->header[2] = 0x0Du; /* Longueur 13 octets */

    uint8_t *sp0 = pkt->subpacket[0];
    sp0[1] = 0x00u; /* Y=0 RGB */
    sp0[2] = 0x08u; /* R=8 Active Aspect Ratio = Picture Aspect Ratio */
    sp0[3] = 0x00u;
    sp0[4] = 0x01u; /* VIC 1 : 640x480p @ 60 Hz */
    sp0[5] = 0x00u;
    hdmi_finalize_infoframe(pkt);
}

void hdmi_packet_set_vendor_infoframe(HdmiPacket *pkt)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->header[0] = 0x81u; /* Vendor Specific InfoFrame */
    pkt->header[1] = 0x01u; /* Version 1 */
    pkt->header[2] = 0x05u; /* Longueur 5 octets */

    /* IEEE Registration Identifier 0x000C03 (HDMI Licensing, LLC) */
    uint8_t *sp0 = pkt->subpacket[0];
    sp0[1] = 0x03u;
    sp0[2] = 0x0Cu;
    sp0[3] = 0x00u;
    hdmi_finalize_infoframe(pkt);
}

void hdmi_packet_set_audio_infoframe(HdmiPacket *pkt)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->header[0] = 0x84u; /* Audio InfoFrame */
    pkt->header[1] = 0x01u; /* Version 1 */
    pkt->header[2] = 0x0Au; /* Longueur 10 octets */

    /*
     * CEA-861-D / HDMI 1.4b §8.2.2 :
     * Pour L-PCM, CT, SF et SS DOIVENT etre a 0 ("Refer to Stream Header").
     * CC = 1 (2 canaux).
     */
    uint8_t *sp0 = pkt->subpacket[0];
    sp0[1] = 0x01u; /* CT=0, CC=1 (2ch) */
    sp0[2] = 0x00u; /* SF=0, SS=0 (Refer to Stream Header) */
    sp0[3] = 0x00u;
    sp0[4] = 0x00u; /* CA=0x00 (FL/FR) */
    sp0[5] = 0x00u; /* LSV=0, DM_INH=0 */
    hdmi_finalize_infoframe(pkt);
}

void HDMI_PKT_FAST_FUNC(hdmi_packet_set_audio_samples)(HdmiPacket *pkt,
                                                       const int16_t left[4],
                                                       const int16_t right[4],
                                                       uint8_t *iecFrameIdx)
{
    uint8_t frame = iecFrameIdx ? *iecFrameIdx : 0u;
    uint8_t b_mask = 0u;

    for (int s = 0; s < 4; s++) {
        uint8_t *d = pkt->subpacket[s];
        uint16_t ul = (uint16_t)left[s];
        uint16_t ur = (uint16_t)right[s];

        uint8_t l_lo = (uint8_t)(ul & 0xFFu);
        uint8_t l_hi = (uint8_t)((ul >> 8) & 0xFFu);
        uint8_t r_lo = (uint8_t)(ur & 0xFFu);
        uint8_t r_hi = (uint8_t)((ur >> 8) & 0xFFu);

        d[0] = 0x00u;
        d[1] = l_lo;
        d[2] = l_hi;
        d[3] = 0x00u;
        d[4] = r_lo;
        d[5] = r_hi;

        uint8_t pl = (uint8_t)(hdmi_parity8_fast(l_lo) ^ hdmi_parity8_fast(l_hi));
        uint8_t pr = (uint8_t)(hdmi_parity8_fast(r_lo) ^ hdmi_parity8_fast(r_hi));

        /*
         * IEC 60958-3 Channel Status (bloc de 192 trames) :
         * - bit 2  (Byte 0 = 0x04) : Consumer L-PCM, copy permitted
         * - bit 25 (Byte 3 = 0x02) : 48 kHz
         * - bit 33 (Byte 4 = 0x02) : 16-bit sample word length
         */
        bool cs = (frame == 2u) || (frame == 25u) || (frame == 33u);
        d[6] = (uint8_t)(((pl << 3) | (pr << 7)) ^ (cs ? 0xCCu : 0x00u));
        d[7] = hdmi_bch7_fast(d);

        if (frame == 0u) {
            b_mask |= (uint8_t)(1u << (s + 4));
        }
        frame++;
        if (frame >= 192u) {
            frame = 0u;
        }
    }

    if (iecFrameIdx) {
        *iecFrameIdx = frame;
    }

    pkt->header[0] = 0x02u;  /* Audio Sample Packet */
    pkt->header[1] = 0x0Fu;  /* sample_present = 0b1111 (4 echantillons valides) */
    pkt->header[2] = b_mask; /* B3..B0 sur bits 7..4, sample_flat = 0 */
    pkt->header[3] = hdmi_bch3_fast(pkt->header);
}

void HDMI_PKT_FAST_FUNC(hdmi_encode_data_island_pio)(HdmiDataIslandPio *out,
                                                     const HdmiPacket *pkt,
                                                     bool vsync_active,
                                                     bool hsync_active)
{
    /* Polarite 640x480p : active-low (0 pendant l'impulsion de synchro, 1 hors impulsion) */
    uint8_t hv = (uint8_t)((!hsync_active ? 1u : 0u) | (!vsync_active ? 2u : 0u));

    /* Leading & Trailing Guard Bands (2 pixels = 1 mot 32 bits chacun) */
    uint16_t gb_ch0    = s_terc4[0x0Cu | hv];
    uint32_t gb0_pair  = HDMI_PAIR_WORD(gb_ch0, gb_ch0);
    uint32_t gb12_pair = HDMI_PAIR_WORD(0x133u, 0x133u);

    out->ch0[0]  = gb0_pair;
    out->ch1[0]  = gb12_pair;
    out->ch2[0]  = gb12_pair;

    out->ch0[17] = gb0_pair;
    out->ch1[17] = gb12_pair;
    out->ch2[17] = gb12_pair;

    uint32_t hdr_bits = (uint32_t)pkt->header[0] |
                        ((uint32_t)pkt->header[1] << 8) |
                        ((uint32_t)pkt->header[2] << 16) |
                        ((uint32_t)pkt->header[3] << 24);

    /* Transposition 4x8 SWAR 32 bits (identique a pico-speccy hdmi_pack_blob_ch0) */
    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t v = (uint32_t)pkt->subpacket[0][i] |
                     ((uint32_t)pkt->subpacket[1][i] << 8) |
                     ((uint32_t)pkt->subpacket[2][i] << 16) |
                     ((uint32_t)pkt->subpacket[3][i] << 24);
        uint32_t t = (v ^ (v >> 7)) & 0x00AA00AAu;
        v = v ^ t ^ (t << 7);
        t = (v ^ (v >> 14)) & 0x0000CCCCu;
        v = v ^ t ^ (t << 14);

        uint32_t pc = i * 4u;

        /* Paire 0 (pixels pc+0 et pc+1) */
        uint8_t c0_0 = (uint8_t)(hv | (((hdr_bits >> (pc + 0u)) & 1u) << 2) | ((pc == 0u) ? 0u : 8u));
        uint8_t c0_1 = (uint8_t)(hv | (((hdr_bits >> (pc + 1u)) & 1u) << 2) | 8u);
        out->ch0[1u + i * 2u] = HDMI_PAIR_WORD(s_terc4[c0_0], s_terc4[c0_1]);
        out->ch1[1u + i * 2u] = HDMI_PAIR_WORD(s_terc4[v & 0x0Fu], s_terc4[(v >> 16) & 0x0Fu]);
        out->ch2[1u + i * 2u] = HDMI_PAIR_WORD(s_terc4[(v >> 8) & 0x0Fu], s_terc4[(v >> 24) & 0x0Fu]);

        /* Paire 1 (pixels pc+2 et pc+3) */
        uint8_t c0_2 = (uint8_t)(hv | (((hdr_bits >> (pc + 2u)) & 1u) << 2) | 8u);
        uint8_t c0_3 = (uint8_t)(hv | (((hdr_bits >> (pc + 3u)) & 1u) << 2) | 8u);
        out->ch0[2u + i * 2u] = HDMI_PAIR_WORD(s_terc4[c0_2], s_terc4[c0_3]);
        out->ch1[2u + i * 2u] = HDMI_PAIR_WORD(s_terc4[(v >> 4) & 0x0Fu], s_terc4[(v >> 20) & 0x0Fu]);
        out->ch2[2u + i * 2u] = HDMI_PAIR_WORD(s_terc4[(v >> 12) & 0x0Fu], s_terc4[(v >> 28) & 0x0Fu]);
    }
}
