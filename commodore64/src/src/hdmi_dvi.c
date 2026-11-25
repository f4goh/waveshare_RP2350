/*
 * Pilote DVI / HDMI PIO0 + DMA pour Waveshare RP2350-PiZero (GPIO 32..39)
 * 640x480p @ 60 Hz (pixel clock 25.2 MHz, bit clock 252.0 MHz, 320x240 logical plein ecran)
 *
 * Supporte dans un meme firmware :
 * - Mode HDMI Audio (s_hdmiAudioEnabled = true) : flux HDMI 1.4b avec Data Islands :
 *     * ACR (N=6144, CTS=25200) sur ligne VBlank 1 et toutes les 4 lignes de VBlank
 *     * AVI InfoFrame (VIC 1 : 640x480p60 RGB) sur ligne VBlank 2
 *     * Vendor Specific InfoFrame (IEEE OUI 0x000C03 HDMI Licensing) sur ligne VBlank 3
 *     * Audio InfoFrame (L-PCM 2ch, CT=0/SF=0/SS=0 Refer to Stream Header) sur ligne VBlank 4
 *     * Audio Sample Packets L-PCM 48.000 kHz verrouilles au ratio exact 32/84 paquet/ligne
 *       (525 * 32 / 84 = 200.000 paquets = 800.000 echantillons par trame, sans aucun
 *       underrun ni coupure du FIFO audio du moniteur/TV)
 * - Mode DVI pur (s_hdmiAudioEnabled = false, pour sorties audio I2S et PWM) :
 *     * En-tetes HBlank DVI purs sans Data Islands.
 */

#include "hdmi_dvi.h"
#include "config.h"
#include "dvi_serialiser.pio.h"
#include "hdmi_packet.h"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <string.h>

#define DVI_PIO          pio0
#define N_LANES          3U
#define SYNC_LANE        0U

/* Timings 640x480p @ 60 Hz */
#define H_FP             16U
#define H_SYNC           96U
#define H_BP             48U
#define H_ACTIVE         640U
#define H_BLANK_WORDS    ((H_FP + H_SYNC + H_BP) / 2U)   /* 80 mots */
#define H_ACTIVE_WORDS   (H_ACTIVE / 2U)                 /* 320 mots */

#define V_FP             10U
#define V_SYNC           2U
#define V_BP             33U
#define V_ACTIVE         480U
#define V_BLANK_TOTAL    (V_FP + V_SYNC + V_BP)          /* 45 lignes */
#define V_TOTAL          (V_BLANK_TOTAL + V_ACTIVE)      /* 525 lignes */

/* Symboles TMDS de controle (2 pixels identiques empaquetes en 32 bits) */
#define DVI_CTRL_00      0x354u /* V=0, H=0 */
#define DVI_CTRL_01      0x0ABu /* V=0, H=1 */
#define DVI_CTRL_10      0x154u /* V=1, H=0 */
#define DVI_CTRL_11      0x2ABu /* V=1, H=1 */
#define DVI_PAIR_WORD(s0, s1) ((((uint32_t)(s1) & 0x3FFu) << 10) | ((uint32_t)(s0) & 0x3FFu))

#define SYM2_CTRL_00     DVI_PAIR_WORD(DVI_CTRL_00, DVI_CTRL_00)
#define SYM2_CTRL_01     DVI_PAIR_WORD(DVI_CTRL_01, DVI_CTRL_01)
#define SYM2_CTRL_10     DVI_PAIR_WORD(DVI_CTRL_10, DVI_CTRL_10)
#define SYM2_CTRL_11     DVI_PAIR_WORD(DVI_CTRL_11, DVI_CTRL_11)

/*
 * Paire TMDS noire equilibree (niveau 0x08 -> 2x 0x307 : 5 bits a 1 et 5 bits a 0
 * dans chaque symbole, disparite 0, swing=3, run=5, identique a pico-speccy).
 */
#define TMDS_BLACK_PAIR  0xC1F07u

typedef struct {
    const void        *read_addr;
    void              *write_addr;
    uint32_t           transfer_count;
    dma_channel_config c;
} DmaCb;

static HdmiScanlineCb s_scanlineCb   = NULL;
static HdmiEndFrameCb s_endFrameCb   = NULL;

static const uint     s_smTmds[N_LANES]  = { 0u, 1u, 2u };
static const uint     s_pinTmds[N_LANES] = { PIN_DVI_D0, PIN_DVI_D1, PIN_DVI_D2 };
static uint           s_chanCtrl[N_LANES];
static uint           s_chanData[N_LANES];

/* Ring 4 buffers TMDS actifs (3 lanes * 320 mots) + 1 ligne noire partagee */
static uint32_t __aligned(4) s_tmdsBuf[4][N_LANES][H_ACTIVE_WORDS];
static uint32_t __aligned(4) s_blackLine[H_ACTIVE_WORDS];

/* 3 Tables séparées de 256 couleurs -> paires TMDS B, G, R en SRAM pour accès ultra-rapide */
static uint32_t __not_in_flash("hdmi_pal") __aligned(4) s_palB[256];
static uint32_t __not_in_flash("hdmi_pal") __aligned(4) s_palG[256];
static uint32_t __not_in_flash("hdmi_pal") __aligned(4) s_palR[256];

/* Mots de controle repetes (ring=2) pour partie "active" des lignes vblank */
static uint32_t __aligned(4) s_vblankCtrlWord[4];

#define HDMI_PREAMBLE_WORD_OFFSET   10U
#define HDMI_PREAMBLE_WORD_COUNT    4U
#define HDMI_ISLAND_WORD_OFFSET     14U

/* Parametres ACR HDMI pour 48.000 kHz a 25.200 MHz (128 * 48000 = 25200000 * 6144 / 25200) */
#define HDMI_ACR_N                  6144U
#define HDMI_ACR_CTS                25200U

/* File SPSC compacte de paquets audio bruts (4 echantillons stereo = 16 octets) */
#define HDMI_AQ_SIZE                128U
#define HDMI_AQ_MASK                (HDMI_AQ_SIZE - 1U)
#define HDMI_AU_TARGET              64U
#define HDMI_AU_DROP_HIGH           96U

typedef struct {
    int16_t l[4];
    int16_t r[4];
} HdmiAudioPkt4;

static HdmiAudioPkt4 s_aqBuf[HDMI_AQ_SIZE];
static volatile uint32_t s_aqWr = 0u;
static volatile uint32_t s_aqRd = 0u;

/*
 * Cadencement rationnel exact verrouille sur l'horloge pixel 25.2 MHz :
 * 48000 Hz / 31500 lignes/s = 32 / 21 echantillons/ligne = 32 / 84 paquet(4 ech)/ligne.
 * Sur 1 trame de 525 lignes : 525 * 32 = 16800 = 200 * 84 -> exactement 200 paquets
 * (800 echantillons) par trame, soit 0.000 ppm de derive par rapport a ACR (N=6144, CTS=25200).
 */
#define HDMI_AU_STEP_LINE           32U
#define HDMI_AU_STEP_PKT            84U
#define HDMI_AU_ACCUM_CAP           (16U * HDMI_AU_STEP_PKT)

static uint32_t s_audioAccum  = 0u;
static uint8_t  s_iecFrameIdx = 0u;
static int16_t  s_lastSampleL = 0;
static int16_t  s_lastSampleR = 0;
static volatile bool s_hdmiAudioEnabled = true;

/* En-tetes HBlank HDMI 1.4b statiques pre-encodes (3 canaux x 80 mots) */
static uint32_t __aligned(4) s_hdrNullActive[N_LANES][H_BLANK_WORDS];
static uint32_t __aligned(4) s_hdrNullVblank[N_LANES][H_BLANK_WORDS];
static uint32_t __aligned(4) s_hdrNullVsync[N_LANES][H_BLANK_WORDS];
static uint32_t __aligned(4) s_hdrVblankAcr[N_LANES][H_BLANK_WORDS];
static uint32_t __aligned(4) s_hdrAvi[N_LANES][H_BLANK_WORDS];
static uint32_t __aligned(4) s_hdrVendor[N_LANES][H_BLANK_WORDS];
static uint32_t __aligned(4) s_hdrAudioInfo[N_LANES][H_BLANK_WORDS];

/* En-tetes HBlank HDMI dynamiques double-bufferises (ping-pong) pour Audio Sample */
static uint32_t __aligned(4) s_hdrDynActive[2][N_LANES][H_BLANK_WORDS];
static uint32_t __aligned(4) s_hdrDynVblank[2][N_LANES][H_BLANK_WORDS];
static uint32_t __aligned(4) s_hdrDynVsync[2][N_LANES][H_BLANK_WORDS];

/* En-tetes HBlank DVI purs (pour modes audio I2S et PWM) */
static uint32_t __aligned(4) s_hdrDviVsync[N_LANES][H_BLANK_WORDS];
static uint32_t __aligned(4) s_hdrDviBlank[N_LANES][H_BLANK_WORDS];

static DmaCb __aligned(16) s_dmaList[2][N_LANES][2];

static uint32_t          s_vScanline  = 0;
static uint32_t          s_frameCount = 0;
static volatile int32_t  s_reqLogY    = -1;
static volatile bool     s_eofPending = false;
static volatile bool     s_core1Ready = false;

/* --------------------------------------------------------------------------
 * Encodage TMDS 0-disparite (swing + run minimaux, clamp [0x08..0xF6])
 * -------------------------------------------------------------------------- */
static uint16_t tmds_qm_8(uint8_t d8)
{
    int s1 = 0;
    for (int i = 0; i < 8; i++) {
        s1 += (d8 & (1u << i)) ? 1 : 0;
    }
    bool is_xnor = (s1 > 4) || ((s1 == 4) && ((d8 & 1u) == 0u));
    uint16_t q = (uint16_t)(d8 & 1u);
    for (int i = 1; i < 8; i++) {
        uint16_t prev = (uint16_t)((q >> (i - 1)) & 1u);
        uint16_t di   = (uint16_t)((d8 >> i) & 1u);
        uint16_t bit  = is_xnor ? (uint16_t)(1u - (prev ^ di)) : (uint16_t)(prev ^ di);
        q |= (uint16_t)(bit << i);
    }
    if (!is_xnor) {
        q |= (1u << 8);
    }
    return q;
}

static inline uint16_t tmds_rep_10(uint16_t q, int invert)
{
    return invert ? (uint16_t)((1u << 9) | (q & 0x100u) | ((~q) & 0xFFu)) : q;
}

static int tmds_pair_swing(uint16_t a, uint16_t b)
{
    int rd = 0, peak = 0;
    for (int k = 0; k < 20; k++) {
        uint16_t c = (k < 10) ? a : b;
        rd += ((c >> (k % 10)) & 1u) ? 1 : -1;
        int m = (rd < 0) ? -rd : rd;
        if (m > peak) peak = m;
    }
    return peak;
}

static int tmds_pair_run(uint16_t a, uint16_t b)
{
    int best = 0, run = 0, prev = -1;
    for (int k = 0; k < 40; k++) {
        int i = k % 20;
        uint16_t c = (i < 10) ? a : b;
        int bit = (int)((c >> (i % 10)) & 1u);
        run = (bit == prev) ? (run + 1) : 1;
        prev = bit;
        if (run > best) best = run;
    }
    return (best > 20) ? 20 : best;
}

static uint32_t tmds_pair_for_level(uint8_t v)
{
    if (v < 0x08u) v = 0x08u;
    if (v > 0xF6u) v = 0xF6u;

    uint16_t qv = tmds_qm_8(v);
    uint8_t cand[3];
    int nc = 0;
    cand[nc++] = v;
    if (v > 0u) cand[nc++] = (uint8_t)(v - 1u);
    if (v < 255u) cand[nc++] = (uint8_t)(v + 1u);
    int best_sw = 99, best_rn = 99;
    uint16_t best_a = tmds_rep_10(qv, 0), best_b = tmds_rep_10(qv, 1);
    for (int c = 0; c < nc; c++) {
        uint16_t qw = tmds_qm_8(cand[c]);
        for (int i = 0; i < 2; i++) {
            uint16_t a = tmds_rep_10(qv, i);
            for (int j = 0; j < 2; j++) {
                uint16_t b = tmds_rep_10(qw, j);
                if (__builtin_popcount((unsigned)a & 0x3FFu) +
                    __builtin_popcount((unsigned)b & 0x3FFu) != 10) {
                    continue;
                }
                int sw = tmds_pair_swing(a, b);
                int rn = tmds_pair_run(a, b);
                if (sw < best_sw || (sw == best_sw && rn < best_rn)) {
                    best_sw = sw;
                    best_rn = rn;
                    best_a  = a;
                    best_b  = b;
                }
            }
        }
    }
    return (((uint32_t)best_b & 0x3FFu) << 10) | ((uint32_t)best_a & 0x3FFu);
}

/* Table cache 0..255 de tmds_pair_for_level() pour que graphics_set_palette soit instantane */
static uint32_t s_levelPairLut[256];
static bool     s_levelLutReady = false;

static void ensure_level_lut(void)
{
    if (s_levelLutReady) return;
    for (int v = 0; v < 256; v++) {
        s_levelPairLut[v] = tmds_pair_for_level((uint8_t)v);
    }
    s_levelLutReady = true;
}

void graphics_set_palette(uint8_t idx, uint32_t color888)
{
    ensure_level_lut();
    uint8_t r = (uint8_t)((color888 >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((color888 >> 8)  & 0xFFu);
    uint8_t b = (uint8_t)(color888 & 0xFFu);

    s_palB[idx] = s_levelPairLut[b]; /* Lane 0 = Blue  */
    s_palG[idx] = s_levelPairLut[g]; /* Lane 1 = Green */
    s_palR[idx] = s_levelPairLut[r]; /* Lane 2 = Red   */
}

/* Palettes C64 16 couleurs : 0 = Pepto PAL, 1 = Colodore */
static const uint32_t s_c64PalettePepto[16] = {
    0x000000, /*  0: Black       */
    0xFFFFFF, /*  1: White       */
    0x68372B, /*  2: Red         */
    0x70A4B2, /*  3: Cyan        */
    0x6F3D86, /*  4: Purple      */
    0x588D43, /*  5: Green       */
    0x352879, /*  6: Blue        */
    0xB8C76F, /*  7: Yellow      */
    0x6F4F25, /*  8: Orange      */
    0x433900, /*  9: Brown       */
    0x9A6759, /* 10: Light Red   */
    0x444444, /* 11: Dark Grey   */
    0x6C6C6C, /* 12: Medium Grey */
    0x9AD284, /* 13: Light Green */
    0x6C5EB5, /* 14: Light Blue  */
    0x959595  /* 15: Light Grey  */
};

static const uint32_t s_c64PaletteColodore[16] = {
    0x000000, /*  0: Black       */
    0xFFFFFF, /*  1: White       */
    0x813338, /*  2: Red         */
    0x75CEC8, /*  3: Cyan        */
    0x8E3C97, /*  4: Purple      */
    0x56AC4D, /*  5: Green       */
    0x2E2C9B, /*  6: Blue        */
    0xEDF171, /*  7: Yellow      */
    0x8E5029, /*  8: Orange      */
    0x553800, /*  9: Brown       */
    0xC46C71, /* 10: Light Red   */
    0x4A4A4A, /* 11: Dark Grey   */
    0x7B7B7B, /* 12: Medium Grey */
    0xA9FF9F, /* 13: Light Green */
    0x706DEB, /* 14: Light Blue  */
    0xB2B2B2  /* 15: Light Grey  */
};

void graphics_restore_c64_palette(uint8_t palette_type)
{
    const uint32_t *pal = (palette_type == 1u) ? s_c64PaletteColodore : s_c64PalettePepto;
    for (int i = 0; i < 16; i++) {
        graphics_set_palette((uint8_t)i, pal[i]);
    }
    /* Couleurs UI 250..255 */
    graphics_set_palette(250, 0x101830);
    graphics_set_palette(251, 0x00FFFF);
    graphics_set_palette(252, 0xFFFF00);
    graphics_set_palette(253, 0x00FF00);
    graphics_set_palette(254, 0x352879);
    graphics_set_palette(255, 0xFFFFFF);
}

void __not_in_flash_func(hdmi_encode_scanline_indexed)(const uint8_t *row320,
                                                       uint32_t *tmds_b,
                                                       uint32_t *tmds_g,
                                                       uint32_t *tmds_r)
{
    const uint32_t *p_in = (const uint32_t *)row320;
    for (uint x = 0; x < H_ACTIVE_WORDS; x += 4) {
        uint32_t p4 = *p_in++;
        uint32_t c0 = p4 & 0xFFu;
        uint32_t c1 = (p4 >> 8) & 0xFFu;
        uint32_t c2 = (p4 >> 16) & 0xFFu;
        uint32_t c3 = (p4 >> 24);

        tmds_b[x]     = s_palB[c0];
        tmds_b[x + 1] = s_palB[c1];
        tmds_b[x + 2] = s_palB[c2];
        tmds_b[x + 3] = s_palB[c3];

        tmds_g[x]     = s_palG[c0];
        tmds_g[x + 1] = s_palG[c1];
        tmds_g[x + 2] = s_palG[c2];
        tmds_g[x + 3] = s_palG[c3];

        tmds_r[x]     = s_palR[c0];
        tmds_r[x + 1] = s_palR[c1];
        tmds_r[x + 2] = s_palR[c2];
        tmds_r[x + 3] = s_palR[c3];
    }
}

static void configure_tmds_pad(uint gpio)
{
    gpio_set_drive_strength(gpio, GPIO_DRIVE_STRENGTH_4MA);
    gpio_set_slew_rate(gpio, GPIO_SLEW_RATE_FAST);
    gpio_set_input_enabled(gpio, false);
}

static void fill_words(uint32_t *dst, uint32_t val, uint count)
{
    for (uint i = 0; i < count; i++) {
        dst[i] = val;
    }
}

static inline __attribute__((always_inline)) void patch_header_island(uint32_t dst[N_LANES][H_BLANK_WORDS],
                                                                      const HdmiDataIslandPio *island)
{
    for (uint i = 0; i < HDMI_DATA_ISLAND_WORDS; i++) {
        dst[0][HDMI_ISLAND_WORD_OFFSET + i] = island->ch0[i];
        dst[1][HDMI_ISLAND_WORD_OFFSET + i] = island->ch1[i];
        dst[2][HDMI_ISLAND_WORD_OFFSET + i] = island->ch2[i];
    }
}

static void build_hdmi_header(uint32_t hdr[N_LANES][H_BLANK_WORDS],
                              bool vsync_active,
                              bool active_video,
                              const HdmiDataIslandPio *island)
{
    uint32_t sym_h1 = vsync_active ? SYM2_CTRL_01 : SYM2_CTRL_11;
    uint32_t sym_h0 = vsync_active ? SYM2_CTRL_00 : SYM2_CTRL_10;

    fill_words(&hdr[0][0],  sym_h1, H_FP / 2u);
    fill_words(&hdr[0][8],  sym_h0, H_SYNC / 2u);
    fill_words(&hdr[0][56], sym_h1, H_BP / 2u);

    fill_words(hdr[1], SYM2_CTRL_00, H_BLANK_WORDS);
    fill_words(hdr[2], SYM2_CTRL_00, H_BLANK_WORDS);

    fill_words(&hdr[1][HDMI_PREAMBLE_WORD_OFFSET], SYM2_CTRL_01, HDMI_PREAMBLE_WORD_COUNT);
    fill_words(&hdr[2][HDMI_PREAMBLE_WORD_OFFSET], SYM2_CTRL_01, HDMI_PREAMBLE_WORD_COUNT);

    patch_header_island(hdr, island);

    if (active_video) {
        fill_words(&hdr[1][H_BLANK_WORDS - 5u], SYM2_CTRL_01, 4u);
        fill_words(&hdr[2][H_BLANK_WORDS - 5u], SYM2_CTRL_00, 4u);

        hdr[0][H_BLANK_WORDS - 1u] = DVI_PAIR_WORD(0x2CCu, 0x2CCu);
        hdr[1][H_BLANK_WORDS - 1u] = DVI_PAIR_WORD(0x133u, 0x133u);
        hdr[2][H_BLANK_WORDS - 1u] = DVI_PAIR_WORD(0x2CCu, 0x2CCu);
    }
}

static void build_dvi_header(uint32_t hdr[N_LANES][H_BLANK_WORDS], bool vsync_active)
{
    uint32_t sym_h1 = vsync_active ? SYM2_CTRL_01 : SYM2_CTRL_11;
    uint32_t sym_h0 = vsync_active ? SYM2_CTRL_00 : SYM2_CTRL_10;

    fill_words(&hdr[0][0],  sym_h1, H_FP / 2u);
    fill_words(&hdr[0][8],  sym_h0, H_SYNC / 2u);
    fill_words(&hdr[0][56], sym_h1, H_BP / 2u);

    fill_words(hdr[1], SYM2_CTRL_00, H_BLANK_WORDS);
    fill_words(hdr[2], SYM2_CTRL_00, H_BLANK_WORDS);
}

void hdmi_dvi_set_audio_enabled(bool enable)
{
    if (s_hdmiAudioEnabled != enable) {
        if (enable) {
            hdmi_dvi_reset_audio_queue();
        }
        s_hdmiAudioEnabled = enable;
    }
}

bool hdmi_dvi_is_audio_enabled(void)
{
    return s_hdmiAudioEnabled;
}

void hdmi_dvi_reset_audio_queue(void)
{
    s_aqRd = 0u;
    s_aqWr = 0u;
    memset(s_aqBuf, 0, sizeof(s_aqBuf));
    s_aqWr        = HDMI_AU_TARGET;
    s_audioAccum  = 0u;
    s_iecFrameIdx = 0u;
    s_lastSampleL = 0;
    s_lastSampleR = 0;
}

void __not_in_flash_func(hdmi_dvi_push_audio_4samples)(const int16_t left[4], const int16_t right[4])
{
    uint32_t wr = s_aqWr;
    uint32_t rd = s_aqRd;
    if ((wr - rd) >= HDMI_AQ_SIZE) {
        return;
    }
    HdmiAudioPkt4 *slot = &s_aqBuf[wr & HDMI_AQ_MASK];
    for (int i = 0; i < 4; i++) {
        slot->l[i] = left[i];
        slot->r[i] = right[i];
    }
    __dmb();
    s_aqWr = wr + 1u;
}

static inline __attribute__((always_inline)) bool hdmi_pop_and_encode_audio(HdmiDataIslandPio *island,
                                                                            bool vsync_active)
{
    if (s_audioAccum < HDMI_AU_STEP_PKT) {
        return false;
    }
    s_audioAccum -= HDMI_AU_STEP_PKT;

    int16_t l[4];
    int16_t r[4];

    uint32_t rd = s_aqRd;
    uint32_t wr = s_aqWr;
    uint32_t qd = wr - rd;

    if (qd != 0u && qd <= HDMI_AQ_SIZE) {
        if (qd > HDMI_AU_DROP_HIGH) {
            rd++;
        }
        const HdmiAudioPkt4 *slot = &s_aqBuf[rd & HDMI_AQ_MASK];
        for (int i = 0; i < 4; i++) {
            l[i] = slot->l[i];
            r[i] = slot->r[i];
        }
        __dmb();
        s_aqRd = rd + 1u;
        s_lastSampleL = l[3];
        s_lastSampleR = r[3];
    } else {
        int16_t curL = s_lastSampleL;
        int16_t curR = s_lastSampleR;
        for (int i = 0; i < 4; i++) {
            curL = (int16_t)(((int32_t)curL * 15) >> 4);
            curR = (int16_t)(((int32_t)curR * 15) >> 4);
            l[i] = curL;
            r[i] = curR;
        }
        s_lastSampleL = curL;
        s_lastSampleR = curR;
    }

    HdmiPacket pkt;
    hdmi_packet_set_audio_samples(&pkt, l, r, &s_iecFrameIdx);
    hdmi_encode_data_island_pio(island, &pkt, vsync_active, true);
    return true;
}

static void init_dma_cb_templates(void)
{
    for (uint p = 0; p < 2u; p++) {
        for (uint l = 0; l < N_LANES; l++) {
            uint cd = s_chanData[l];
            uint cc = s_chanCtrl[l];

            DmaCb *cb0 = &s_dmaList[p][l][0];
            cb0->read_addr      = s_hdmiAudioEnabled ? s_hdrNullVblank[l] : s_hdrDviBlank[l];
            cb0->write_addr     = (void *)&DVI_PIO->txf[s_smTmds[l]];
            cb0->transfer_count = H_BLANK_WORDS;
            cb0->c = dma_channel_get_default_config(cd);
            channel_config_set_dreq(&cb0->c, pio_get_dreq(DVI_PIO, s_smTmds[l], true));
            channel_config_set_chain_to(&cb0->c, cc);
            channel_config_set_irq_quiet(&cb0->c, (l != SYNC_LANE));

            DmaCb *cb1 = &s_dmaList[p][l][1];
            cb1->read_addr      = &s_vblankCtrlWord[0];
            cb1->write_addr     = (void *)&DVI_PIO->txf[s_smTmds[l]];
            cb1->transfer_count = H_ACTIVE_WORDS;
            cb1->c = dma_channel_get_default_config(cd);
            channel_config_set_ring(&cb1->c, false, 2);
            channel_config_set_dreq(&cb1->c, pio_get_dreq(DVI_PIO, s_smTmds[l], true));
            channel_config_set_chain_to(&cb1->c, cc);
            channel_config_set_irq_quiet(&cb1->c, true);
        }
    }
}

static inline __attribute__((always_inline)) void prepare_scanline_dma(uint32_t line, uint pong)
{
    bool vsync_active = (line >= V_FP && line < V_FP + V_SYNC);
    bool active_video = (line >= V_BLANK_TOTAL);

    const uint32_t *hdr_lanes[N_LANES];
    const uint32_t *data_lanes[N_LANES];
    uint data_ring = active_video ? 0u : 2u;

    if (s_hdmiAudioEnabled) {
        s_audioAccum += HDMI_AU_STEP_LINE;
        if (s_audioAccum > HDMI_AU_ACCUM_CAP) {
            s_audioAccum = HDMI_AU_ACCUM_CAP;
        }

        uint32_t (*hdr)[H_BLANK_WORDS] = NULL;
        HdmiDataIslandPio island;

        if (!active_video) {
            if (vsync_active) {
                if (hdmi_pop_and_encode_audio(&island, true)) {
                    patch_header_island(s_hdrDynVsync[pong], &island);
                    hdr = s_hdrDynVsync[pong];
                } else {
                    hdr = s_hdrNullVsync;
                }
            } else if (line == 1u) {
                hdr = s_hdrVblankAcr;
            } else if (line == 2u) {
                hdr = s_hdrAvi;
            } else if (line == 3u) {
                hdr = s_hdrVendor;
            } else if (line == 4u) {
                hdr = s_hdrAudioInfo;
            } else if (line >= (V_FP + V_SYNC) && ((line & 3u) == 0u)) {
                hdr = s_hdrVblankAcr;
            } else if (hdmi_pop_and_encode_audio(&island, false)) {
                patch_header_island(s_hdrDynVblank[pong], &island);
                hdr = s_hdrDynVblank[pong];
            } else {
                hdr = s_hdrNullVblank;
            }
        } else {
            if (hdmi_pop_and_encode_audio(&island, false)) {
                patch_header_island(s_hdrDynActive[pong], &island);
                hdr = s_hdrDynActive[pong];
            } else {
                hdr = s_hdrNullActive;
            }
        }

        for (uint l = 0; l < N_LANES; l++) {
            hdr_lanes[l] = hdr[l];
        }
    } else {
        for (uint l = 0; l < N_LANES; l++) {
            hdr_lanes[l] = vsync_active ? s_hdrDviVsync[l] : s_hdrDviBlank[l];
        }
    }

    if (active_video) {
        uint32_t active_y = line - V_BLANK_TOTAL; /* 0..479 */
        uint32_t log_y    = active_y >> 1;        /* 0..239 */
        uint buf_idx      = log_y & 3u;

        for (uint l = 0; l < N_LANES; l++) {
            data_lanes[l] = s_tmdsBuf[buf_idx][l];
        }

        /* Planifie le rendu Core 1 avec 1 ligne logique d'avance (63.5 us de marge) */
        if ((active_y & 1u) == 0u && log_y < 239u) {
            s_reqLogY = (int32_t)(log_y + 1u);
            __sev();
        }
    } else {
        data_lanes[0] = &s_vblankCtrlWord[vsync_active ? 1u : 3u];
        data_lanes[1] = &s_vblankCtrlWord[0];
        data_lanes[2] = &s_vblankCtrlWord[0];
        if (line == V_FP) {
            s_frameCount++;
            s_eofPending = true;
            __sev();
        } else if (line == (V_BLANK_TOTAL - 2u)) {
            /* Pre-rendu de la ligne logique 0 avant le debut de la zone active */
            s_reqLogY = 0;
            __sev();
        }
    }

    for (uint l = 0; l < N_LANES; l++) {
        s_dmaList[pong][l][0].read_addr = hdr_lanes[l];
        s_dmaList[pong][l][1].read_addr = data_lanes[l];
        channel_config_set_ring(&s_dmaList[pong][l][1].c, false, data_ring);
    }
}

static void __not_in_flash_func(hdmi_dma_irq0)(void)
{
    dma_hw->ints0 = 1u << s_chanData[SYNC_LANE];

    for (uint l = 0; l < N_LANES; l++) {
        while (dma_channel_is_busy(s_chanCtrl[l]) ||
               dma_debug_hw->ch[s_chanData[l]].dbg_tcr <= H_BLANK_WORDS) {
            tight_loop_contents();
        }
    }

    s_vScanline++;
    if (s_vScanline >= V_TOTAL) {
        s_vScanline = 0;
    }

    uint pong = s_vScanline & 1u;

    for (uint l = 0; l < N_LANES; l++) {
        dma_channel_hw_t *ctrl = &dma_hw->ch[s_chanCtrl[l]];
        ctrl->read_addr      = (uintptr_t)&s_dmaList[pong][l][0];
        ctrl->transfer_count = 4u;
    }

    prepare_scanline_dma(s_vScanline, pong);
}

static void __not_in_flash_func(core1_video_worker)(void)
{
    for (uint l = 0; l < N_LANES; l++) {
        dma_channel_config cfg = dma_channel_get_default_config(s_chanCtrl[l]);
        channel_config_set_ring(&cfg, true, 4);
        channel_config_set_read_increment(&cfg, true);
        channel_config_set_write_increment(&cfg, true);
        dma_channel_configure(
            s_chanCtrl[l],
            &cfg,
            &dma_hw->ch[s_chanData[l]],
            &s_dmaList[0][l][0],
            4u,
            false);
    }

    uint32_t mask_sync = 1u << s_chanData[SYNC_LANE];
    dma_hw->ints0 = mask_sync;
    hw_set_bits(&dma_hw->inte0, mask_sync);
    irq_set_exclusive_handler(DMA_IRQ_0, hdmi_dma_irq0);
    irq_set_priority(DMA_IRQ_0, 0);
    irq_set_enabled(DMA_IRQ_0, true);

    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    dma_start_channel_mask(
        (1u << s_chanCtrl[0]) |
        (1u << s_chanCtrl[1]) |
        (1u << s_chanCtrl[2]));

    for (uint l = 0; l < N_LANES; l++) {
        while (!pio_sm_is_tx_fifo_full(DVI_PIO, s_smTmds[l])) {
            tight_loop_contents();
        }
    }

    uint sm_mask = (1u << s_smTmds[0]) | (1u << s_smTmds[1]) | (1u << s_smTmds[2]);
    hw_set_bits(&DVI_PIO->ctrl, sm_mask << PIO_CTRL_SM_ENABLE_LSB);
    pwm_set_enabled(pwm_gpio_to_slice_num(PIN_DVI_CLK), true);

    s_core1Ready = true;

    while (1) {
        int32_t log_y = s_reqLogY;
        if (log_y >= 0 && log_y < (int32_t)HDMI_LOGICAL_HEIGHT) {
            s_reqLogY = -1;
            uint buf_idx = (uint)log_y & 3u;
            if (s_scanlineCb) {
                s_scanlineCb((uint16_t)log_y, s_tmdsBuf[buf_idx][0], s_tmdsBuf[buf_idx][1], s_tmdsBuf[buf_idx][2]);
            }
        } else if (s_eofPending) {
            s_eofPending = false;
            if (s_endFrameCb) {
                s_endFrameCb(s_frameCount);
            }
        } else {
            __wfe();
        }
    }
}

void hdmi_dvi_init(HdmiScanlineCb scanline_cb, HdmiEndFrameCb endframe_cb)
{
    s_scanlineCb = scanline_cb;
    s_endFrameCb = endframe_cb;
    s_vScanline  = 0;
    s_frameCount = 0;
    s_reqLogY    = -1;
    s_eofPending = false;
    s_core1Ready = false;

    ensure_level_lut();
    for (int i = 0; i < 256; i++) {
        s_palB[i] = TMDS_BLACK_PAIR;
        s_palG[i] = TMDS_BLACK_PAIR;
        s_palR[i] = TMDS_BLACK_PAIR;
    }
    graphics_restore_c64_palette(0u);

    for (uint i = 0; i < H_ACTIVE_WORDS; i++) {
        s_blackLine[i] = TMDS_BLACK_PAIR;
    }
    for (uint b = 0; b < 4u; b++) {
        for (uint l = 0; l < N_LANES; l++) {
            for (uint i = 0; i < H_ACTIVE_WORDS; i++) {
                s_tmdsBuf[b][l][i] = TMDS_BLACK_PAIR;
            }
        }
    }

    s_vblankCtrlWord[0] = SYM2_CTRL_00;
    s_vblankCtrlWord[1] = SYM2_CTRL_01;
    s_vblankCtrlWord[2] = SYM2_CTRL_10;
    s_vblankCtrlWord[3] = SYM2_CTRL_11;

    /* 1. En-tetes DVI purs (utilises lorsque la sortie audio est I2S ou PWM) */
    build_dvi_header(s_hdrDviVsync, true);
    build_dvi_header(s_hdrDviBlank, false);

    /* 2. En-tetes HDMI 1.4b Data Islands (utilises lorsque la sortie audio est HDMI) */
    HdmiPacket pkt;
    HdmiDataIslandPio island;

    hdmi_packet_set_null(&pkt);
    hdmi_encode_data_island_pio(&island, &pkt, false, true);
    build_hdmi_header(s_hdrNullActive, false, true, &island);
    build_hdmi_header(s_hdrNullVblank, false, false, &island);

    hdmi_encode_data_island_pio(&island, &pkt, true, true);
    build_hdmi_header(s_hdrNullVsync, true, false, &island);

    hdmi_packet_set_acr(&pkt, HDMI_ACR_N, HDMI_ACR_CTS);
    hdmi_encode_data_island_pio(&island, &pkt, false, true);
    build_hdmi_header(s_hdrVblankAcr, false, false, &island);

    hdmi_packet_set_avi_infoframe(&pkt);
    hdmi_encode_data_island_pio(&island, &pkt, false, true);
    build_hdmi_header(s_hdrAvi, false, false, &island);

    hdmi_packet_set_vendor_infoframe(&pkt);
    hdmi_encode_data_island_pio(&island, &pkt, false, true);
    build_hdmi_header(s_hdrVendor, false, false, &island);

    hdmi_packet_set_audio_infoframe(&pkt);
    hdmi_encode_data_island_pio(&island, &pkt, false, true);
    build_hdmi_header(s_hdrAudioInfo, false, false, &island);

    for (uint b = 0; b < 2u; b++) {
        memcpy(s_hdrDynActive[b], s_hdrNullActive, sizeof(s_hdrNullActive));
        memcpy(s_hdrDynVblank[b], s_hdrNullVblank, sizeof(s_hdrNullVblank));
        memcpy(s_hdrDynVsync[b],  s_hdrNullVsync,  sizeof(s_hdrNullVsync));
    }

    hdmi_dvi_reset_audio_queue();

    /* Initialisation PIO0 avec gpio_base = 16 pour atteindre GPIO 32..39 */
    pio_set_gpio_base(DVI_PIO, 16u);
    uint offset = pio_add_program(DVI_PIO, &dvi_serialiser_program);
    (void)offset;

    for (uint l = 0; l < N_LANES; l++) {
        pio_sm_claim(DVI_PIO, s_smTmds[l]);
        dvi_serialiser_program_init(DVI_PIO, s_smTmds[l], 0u, s_pinTmds[l]);
        configure_tmds_pad(s_pinTmds[l]);
        configure_tmds_pad(s_pinTmds[l] + 1u);

        s_chanCtrl[l] = dma_claim_unused_channel(true);
        s_chanData[l] = dma_claim_unused_channel(true);
    }

    /* Horloge TMDS PWM (GPIO 38/39 = Slice 11) */
    uint clk_slice = pwm_gpio_to_slice_num(PIN_DVI_CLK);
    pwm_config pwm_cfg = pwm_get_default_config();
    pwm_config_set_output_polarity(&pwm_cfg, true, false);
    pwm_config_set_wrap(&pwm_cfg, 9);
    pwm_init(clk_slice, &pwm_cfg, false);
    pwm_set_both_levels(clk_slice, 5, 5);
    gpio_set_function(PIN_DVI_CLK, GPIO_FUNC_PWM);
    gpio_set_function(PIN_DVI_CLK + 1u, GPIO_FUNC_PWM);
    configure_tmds_pad(PIN_DVI_CLK);
    configure_tmds_pad(PIN_DVI_CLK + 1u);

    init_dma_cb_templates();
    prepare_scanline_dma(0u, 0u);
    s_audioAccum = 0u;
}

void hdmi_dvi_launch_core1(void)
{
    multicore_launch_core1(core1_video_worker);
    while (!s_core1Ready) {
        tight_loop_contents();
    }
}
