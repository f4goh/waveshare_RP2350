/*
 * Emulation ULA Oric Atmos + encodage TMDS direct pour sortie HDMI/DVI 320x240
 */

#include "ula.h"
#include "bus.h"
#include "ps2_kbd.h"
#include "pico/stdlib.h"

#include <string.h>

extern const uint8_t charset[];

/* Paires TMDS 0-disparite precalculees par couleur Oric (8 x RGB) */
static uint32_t s_tmdsPalRgb[8][3];
static uint8_t  s_curPalette = 0xFFu;

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

/*
 * Construction de paire TMDS 0-disparite (identique a pico-speccy tmds_pair.h
 * + HDMI_TMDS_LEVEL_CLAMP [0x08..0xF6]) :
 * Evite les motifs extremes 0x00/0xFF (swing=9, run=9) qui font deriver le CDR
 * HDMI et provoquent des erreurs BCH sur les Data Islands audio qui suivent.
 */
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
    return ((uint32_t)best_b << 10) | (uint32_t)best_a;
}

void ula_set_palette(Ula *u, uint8_t palette)
{
    if (u) {
        u->palette = palette;
    }
    if (palette > (uint8_t)ULA_PAL_MONO) {
        palette = (uint8_t)ULA_PAL_RGB;
    }
    if (s_curPalette == palette) {
        return;
    }
    s_curPalette = palette;

    /* Luminance perceptuelle approx (0..7 -> 0..255) pour modes CRT monochromes */
    static const uint8_t kLuma[8] = {
        0u, 85u, 150u, 225u, 45u, 115u, 195u, 255u
    };

    for (uint8_t c = 0u; c < 8u; c++) {
        uint8_t r, g, b;
        if (palette == (uint8_t)ULA_PAL_RGB) {
            r = (c & 1u) ? 255u : 0u;
            g = (c & 2u) ? 255u : 0u;
            b = (c & 4u) ? 255u : 0u;
        } else {
            uint8_t l = kLuma[c];
            if (palette == (uint8_t)ULA_PAL_GREEN) {
                r = (uint8_t)(l >> 3);
                g = l;
                b = (uint8_t)(l >> 2);
            } else if (palette == (uint8_t)ULA_PAL_AMBER) {
                r = l;
                g = (uint8_t)((l * 145u) >> 8);
                b = 0u;
            } else {
                r = l;
                g = l;
                b = l;
            }
        }
        s_tmdsPalRgb[c][0] = tmds_pair_for_level(r);
        s_tmdsPalRgb[c][1] = tmds_pair_for_level(g);
        s_tmdsPalRgb[c][2] = tmds_pair_for_level(b);
    }
}

void ula_init(Ula *u)
{
    ps2kbd_begin();
    ula_set_palette(u, (uint8_t)ULA_PAL_RGB);
    ula_reset(u);
}

void ula_reset(Ula *u)
{
    if (s_curPalette == 0xFFu) {
        ula_set_palette(u, (uint8_t)ULA_PAL_RGB);
    } else if (u) {
        u->palette = s_curPalette;
    }
    u->pattr         = 0;
    u->pattrSave     = 0;
    u->blink_counter = 0;
    u->enMonitor     = false;
}

void ula_monitor(Ula *u, bool enable)
{
    if (enable && !u->enMonitor) {
        u->enMonitor = true;
        u->pattrSave = u->pattr;
        u->pattr    &= (uint8_t)~PATTR_HIRES;
    } else if (!enable && u->enMonitor) {
        u->enMonitor = false;
        u->pattr     = u->pattrSave;
    }
}

void __not_in_flash_func(ula_render_oric_line)(
    uint8_t       *pattr_io,
    uint16_t       y,
    const uint8_t *oric_ram,
    const uint8_t *text_override,
    const uint8_t *charset_override,
    bool           blink_hide,
    uint8_t        fb_out[ORIC_FRAMEBUFFER_SIZE])
{
    uint8_t  pattr = *pattr_io;
    uint8_t  lattr = 0;
    uint8_t  fgcol = 7;
    uint8_t  bgcol = 0;
    uint8_t *p     = fb_out;

    for (uint16_t x = 0; x < ORIC_SCREEN_COLS; x++) {
        uint8_t ch, pat;

        if ((pattr & PATTR_HIRES) && y < 200u) {
            ch = pat = oric_ram[ORIC_HIRES_ADDR + y * ORIC_SCREEN_COLS + x];
        } else {
            uint16_t cell = (uint16_t)((y >> 3) * ORIC_SCREEN_COLS + x);
            ch = text_override ? text_override[cell]
                               : oric_ram[ORIC_SCREEN_ADDR + cell];

            int off = ((lattr & LATTR_DSIZE) ? (y >> 1) : y) & 7;
            const uint8_t *base;
            if (pattr & PATTR_HIRES) {
                base = oric_ram + ((lattr & LATTR_ALT) ? 0x9C00u : 0x9800u);
            } else if (lattr & LATTR_ALT) {
                base = oric_ram + 0xB800u;
            } else {
                base = charset_override ? charset_override : (oric_ram + 0xB400u);
            }
            pat = base[((ch & 0x7Fu) << 3) | (uint16_t)off];
        }

        if (!(ch & 0x60u)) {
            pat = 0x00u;
            switch (ch & 0x18u) {
                case 0x00u: fgcol = ch & 7u; break;
                case 0x08u: lattr = ch & 7u; break;
                case 0x10u: bgcol = ch & 7u; break;
                case 0x18u: pattr = ch & 7u; break;
            }
        }

        uint8_t c_fg = fgcol;
        uint8_t c_bg = bgcol;

        if (ch & 0x80u) {
            c_fg ^= 0x07u;
            c_bg ^= 0x07u;
        }
        if ((lattr & LATTR_BLINK) && blink_hide) {
            c_fg = c_bg;
        }

        uint8_t c0 = (pat & 0x20u) ? c_fg : c_bg;
        uint8_t c1 = (pat & 0x10u) ? c_fg : c_bg;
        *p++ = (uint8_t)((c0 << 4) | c1);

        c0 = (pat & 0x08u) ? c_fg : c_bg;
        c1 = (pat & 0x04u) ? c_fg : c_bg;
        *p++ = (uint8_t)((c0 << 4) | c1);

        c0 = (pat & 0x02u) ? c_fg : c_bg;
        c1 = (pat & 0x01u) ? c_fg : c_bg;
        *p++ = (uint8_t)((c0 << 4) | c1);
    }

    *pattr_io = pattr;
}

void __not_in_flash_func(ula_scanline_tmds)(
    Ula      *u,
    uint16_t  y_oric,
    uint32_t *tmds_b,
    uint32_t *tmds_g,
    uint32_t *tmds_r)
{
    bool blink_hide = (u->blink_counter & 0x10) != 0;
    uint8_t pattr   = u->enMonitor ? 0u : u->pattr;

    ula_render_oric_line(&pattr,
                         y_oric,
                         ram,
                         u->enMonitor ? ramMoni : NULL,
                         u->enMonitor ? charset : NULL,
                         blink_hide,
                         u->fb);

    if (!u->enMonitor) {
        u->pattr = pattr;
    }

    /* Centrage dans 320 px : bordures [0..39] et [280..319] deja initialisees a noir */
    uint32_t *pb = tmds_b + 40;
    uint32_t *pg = tmds_g + 40;
    uint32_t *pr = tmds_r + 40;

    for (uint i = 0; i < ORIC_FRAMEBUFFER_SIZE; i++) {
        uint8_t v  = u->fb[i];
        uint8_t c0 = (uint8_t)((v >> 4) & 0x07u);
        uint8_t c1 = (uint8_t)(v & 0x07u);

        *pr++ = s_tmdsPalRgb[c0][0];
        *pg++ = s_tmdsPalRgb[c0][1];
        *pb++ = s_tmdsPalRgb[c0][2];

        *pr++ = s_tmdsPalRgb[c1][0];
        *pg++ = s_tmdsPalRgb[c1][1];
        *pb++ = s_tmdsPalRgb[c1][2];
    }
}

void __not_in_flash_func(ula_endframe)(Ula *u)
{
    for (int i = 0; i < 8; i++) {
        uint8_t c = ps2kbd_read();
        if (c == 0u) {
            break;
        }
        ps2_queue_push(c);
    }

    u->blink_counter = (u->blink_counter + 1) & 0x1F;
}
