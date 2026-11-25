/*
 * Stockage et application de la configuration emulatoreur (/oric.cfg)
 */

#ifndef ORIC_CONFIG_STORE_H
#define ORIC_CONFIG_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORIC_CFG_FILENAME "oric.cfg"

typedef struct {
    bool    microdisc_mode;  /* false=Atmos Simple (TAP), true=Atmos Microdisc (DSK) */
    uint8_t audio_out_mode;  /* 0=HDMI 48kHz, 1=I2S PCM5102, 2=PWM GPIO26 */
    bool    azerty_kbd;
    bool    audio_mute;
    uint8_t audio_volume;    /* 25, 50, 75, 100 */
    bool    audio_dc_filter;
    uint8_t video_palette;   /* 0=RGB, 1=Vert, 2=Ambre, 3=Mono */
    uint8_t nes_preset;      /* 0=Perso, 1=Fleches, 2=ZQSD, 3=WASD, 4=IJKL */
    uint8_t nes_col[8];      /* Colonne matrice Oric par bouton NES (0..7, 255=aucun) */
    uint8_t nes_row[8];      /* Ligne matrice Oric par bouton NES (0..7, 255=aucun) */
} OricConfig;

void        config_set_defaults(OricConfig *cfg);
void        config_parse_text(const char *txt, size_t len, OricConfig *cfg);
size_t      config_format_text(const OricConfig *cfg, char *out, size_t cap);

OricConfig *config_get(void);
bool        config_load(void);
bool        config_save(void);
void        config_apply(const OricConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ORIC_CONFIG_STORE_H */
