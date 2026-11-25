/*
 * Persistance /oric.cfg (format texte cle=valeur) et application live
 */

#include "config_store.h"
#include "arduino_hal.h"
#include "audio.h"
#include "microdisc.h"
#include "oric_keyboard.h"
#include "ps2_kbd.h"
#include "ula.h"
#include "usb_kbd.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static OricConfig s_cfg;
static bool       s_cfgInit = false;

void config_set_defaults(OricConfig *cfg)
{
    if (!cfg) {
        return;
    }
    cfg->microdisc_mode  = false;
#if defined(AUDIO_BACKEND_I2S)
    cfg->audio_out_mode  = (uint8_t)AUDIO_OUT_I2S;
#elif defined(AUDIO_BACKEND_PWM)
    cfg->audio_out_mode  = (uint8_t)AUDIO_OUT_PWM;
#else
    cfg->audio_out_mode  = (uint8_t)AUDIO_OUT_HDMI;
#endif
    cfg->azerty_kbd      = false;
    cfg->audio_mute      = false;
    cfg->audio_volume    = 100u;
    cfg->audio_dc_filter = true;
    cfg->video_palette   = (uint8_t)ULA_PAL_RGB;
    cfg->nes_preset      = 0u;
    /* Defaut manette NES : Droite, Gauche, Bas, Haut, Start(ShiftG), Select(ShiftD), B(Espace), A(Espace) */
    static const uint8_t def_c[8] = {7, 5, 6, 3, 4, 4, 0, 0};
    static const uint8_t def_r[8] = {4, 4, 4, 4, 4, 7, 4, 4};
    memcpy(cfg->nes_col, def_c, 8u);
    memcpy(cfg->nes_row, def_r, 8u);
}

static uint8_t clamp_volume(int v)
{
    if (v <= 25) return 25u;
    if (v <= 50) return 50u;
    if (v <= 75) return 75u;
    return 100u;
}

void config_parse_text(const char *txt, size_t len, OricConfig *cfg)
{
    if (!cfg) {
        return;
    }
    config_set_defaults(cfg);
    if (!txt || len == 0u) {
        return;
    }

    size_t pos = 0u;
    while (pos < len) {
        while (pos < len && (txt[pos] == '\r' || txt[pos] == '\n' || txt[pos] == ' ' || txt[pos] == '\t')) {
            pos++;
        }
        if (pos >= len) {
            break;
        }
        size_t end = pos;
        while (end < len && txt[end] != '\r' && txt[end] != '\n') {
            end++;
        }

        char line[80];
        size_t l = end - pos;
        if (l >= sizeof(line)) {
            l = sizeof(line) - 1u;
        }
        memcpy(line, txt + pos, l);
        line[l] = '\0';
        pos = end;

        if (line[0] == '#' || line[0] == ';' || line[0] == '\0') {
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char *k = line;
        int val = atoi(eq + 1);

        if (strcmp(k, "microdisc") == 0) {
            cfg->microdisc_mode = (val != 0);
        } else if (strcmp(k, "audio_out") == 0) {
            cfg->audio_out_mode = (val >= 0 && val <= 2) ? (uint8_t)val : 0u;
        } else if (strcmp(k, "azerty") == 0) {
            cfg->azerty_kbd = (val != 0);
        } else if (strcmp(k, "mute") == 0) {
            cfg->audio_mute = (val != 0);
        } else if (strcmp(k, "volume") == 0) {
            cfg->audio_volume = clamp_volume(val);
        } else if (strcmp(k, "dc_filter") == 0) {
            cfg->audio_dc_filter = (val != 0);
        } else if (strcmp(k, "palette") == 0) {
            cfg->video_palette = (val >= 0 && val <= 3) ? (uint8_t)val : 0u;
        } else if (strcmp(k, "nes_preset") == 0) {
            cfg->nes_preset = (val >= 0 && val <= 4) ? (uint8_t)val : 0u;
        } else if (strcmp(k, "nes_map") == 0) {
            const char *p = eq + 1;
            for (int i = 0; i < 8 && *p; i++) {
                unsigned int c = 255u, r = 255u;
                if (sscanf(p, "%u,%u", &c, &r) == 2) {
                    cfg->nes_col[i] = (c <= 7u) ? (uint8_t)c : 255u;
                    cfg->nes_row[i] = (r <= 7u) ? (uint8_t)r : 255u;
                }
                const char *semi = strchr(p, ';');
                if (!semi) break;
                p = semi + 1;
            }
        }
    }
}

size_t config_format_text(const OricConfig *cfg, char *out, size_t cap)
{
    if (!cfg || !out || cap == 0u) {
        return 0u;
    }
    int n = snprintf(out, cap,
                     "# Oric Atmos RP2350-PiZero config\n"
                     "microdisc=%d\n"
                     "audio_out=%u\n"
                     "azerty=%d\n"
                     "mute=%d\n"
                     "volume=%u\n"
                     "dc_filter=%d\n"
                     "palette=%u\n"
                     "nes_preset=%u\n"
                     "nes_map=%u,%u;%u,%u;%u,%u;%u,%u;%u,%u;%u,%u;%u,%u;%u,%u\n",
                     cfg->microdisc_mode ? 1 : 0,
                     (unsigned)cfg->audio_out_mode,
                     cfg->azerty_kbd ? 1 : 0,
                     cfg->audio_mute ? 1 : 0,
                     (unsigned)cfg->audio_volume,
                     cfg->audio_dc_filter ? 1 : 0,
                     (unsigned)cfg->video_palette,
                     (unsigned)cfg->nes_preset,
                     (unsigned)cfg->nes_col[0], (unsigned)cfg->nes_row[0],
                     (unsigned)cfg->nes_col[1], (unsigned)cfg->nes_row[1],
                     (unsigned)cfg->nes_col[2], (unsigned)cfg->nes_row[2],
                     (unsigned)cfg->nes_col[3], (unsigned)cfg->nes_row[3],
                     (unsigned)cfg->nes_col[4], (unsigned)cfg->nes_row[4],
                     (unsigned)cfg->nes_col[5], (unsigned)cfg->nes_row[5],
                     (unsigned)cfg->nes_col[6], (unsigned)cfg->nes_row[6],
                     (unsigned)cfg->nes_col[7], (unsigned)cfg->nes_row[7]);
    if (n < 0) {
        out[0] = '\0';
        return 0u;
    }
    return ((size_t)n < cap) ? (size_t)n : (cap - 1u);
}

OricConfig *config_get(void)
{
    if (!s_cfgInit) {
        config_set_defaults(&s_cfg);
        s_cfgInit = true;
    }
    return &s_cfg;
}

void config_apply(const OricConfig *cfg)
{
    if (!cfg) {
        return;
    }
    microdisc_set_enabled(cfg->microdisc_mode);
    audioSetOutputMode(cfg->audio_out_mode);
    usb_hid_set_azerty(cfg->azerty_kbd);
    ps2_set_azerty(cfg->azerty_kbd);
    audioSetMute(cfg->audio_mute);
    audioSetVolume(cfg->audio_volume);
    audioSetDcFilter(cfg->audio_dc_filter);
    videoSetPalette(cfg->video_palette);
    if (cfg->nes_preset == 0u) {
        for (uint8_t i = 0u; i < 8u; i++) {
            oric_keyboard_set_nes_key(i, cfg->nes_col[i], cfg->nes_row[i]);
        }
    } else {
        oric_keyboard_set_nes_preset(cfg->nes_preset);
    }
}

bool config_load(void)
{
    OricConfig *cfg = config_get();
    config_set_defaults(cfg);

    uint8_t buf[384];
    size_t  br = 0u;
    if (hal_sd_read_file(ORIC_CFG_FILENAME, buf, sizeof(buf) - 1u, &br) != 0) {
        config_apply(cfg);
        return false;
    }
    buf[br] = '\0';
    config_parse_text((const char *)buf, br, cfg);
    /* Securite au demarrage : ne pas booter en mode Microdisc sans disquette .DSK montee */
    if (cfg->microdisc_mode && !microdisc_has_disk(0u)) {
        cfg->microdisc_mode = false;
    }
    config_apply(cfg);
    return true;
}

bool config_save(void)
{
    OricConfig *cfg = config_get();
    cfg->nes_preset = oric_keyboard_get_nes_preset();
    for (uint8_t i = 0u; i < 8u; i++) {
        oric_keyboard_get_nes_key(i, &cfg->nes_col[i], &cfg->nes_row[i]);
    }
    char buf[384];
    size_t n = config_format_text(cfg, buf, sizeof(buf));
    if (n == 0u) {
        return false;
    }
    return hal_sd_write_file(ORIC_CFG_FILENAME, (const uint8_t *)buf, n) == (int)n;
}
