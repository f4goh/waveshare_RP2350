/*
 * Emulation ULA Oric Atmos (rendu ligne texte/HIRES, clignotement, mode moniteur)
 */

#ifndef ORIC_ULA_H
#define ORIC_ULA_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

#define ORIC_FRAMEBUFFER_SIZE (ORIC_SCREEN_WIDTH / 2U) /* 120 octets = 240 pixels 4-bit */

#define PATTR_50HZ  0x02U
#define PATTR_HIRES 0x04U

#define LATTR_ALT   0x01U
#define LATTR_DSIZE 0x02U
#define LATTR_BLINK 0x04U

typedef enum {
    ULA_PAL_RGB   = 0,
    ULA_PAL_GREEN = 1,
    ULA_PAL_AMBER = 2,
    ULA_PAL_MONO  = 3
} UlaPalette;

typedef struct {
    int     blink_counter;
    uint8_t pattr;
    uint8_t pattrSave;
    uint8_t palette;
    bool    enMonitor;
    uint8_t fb[ORIC_FRAMEBUFFER_SIZE];
} Ula;

void    ula_init(Ula *u);
void    ula_reset(Ula *u);
void    ula_monitor(Ula *u, bool enable);
void    ula_set_palette(Ula *u, uint8_t palette);

/* Rendu d'une scanline Oric (240 pixels compactes en 120 octets 4-bit)
 * Partage entre la sortie HDMI/DVI temps reel et l'export BMP. */
void    ula_render_oric_line(uint8_t       *pattr_io,
                             uint16_t       y,
                             const uint8_t *oric_ram,
                             const uint8_t *text_override,
                             const uint8_t *charset_override,
                             bool           blink_hide,
                             uint8_t        fb_out[ORIC_FRAMEBUFFER_SIZE]);

void    ula_scanline_tmds(Ula      *u,
                          uint16_t  y_oric,
                          uint32_t *tmds_b,
                          uint32_t *tmds_g,
                          uint32_t *tmds_r);
void    ula_endframe(Ula *u);

#endif /* ORIC_ULA_H */
