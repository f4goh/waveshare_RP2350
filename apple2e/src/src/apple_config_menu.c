/*
 * apple_config_menu.c - Menu de Configuration OSD Apple IIe (Waveshare RP2350-PiZero)
 * Accessible via F4 / F12 ou Select+Start sur manette NES.
 */

#include "apple_config_menu.h"
#include "audio.h"
#include "hdmi_dvi.h"
#include "disk_ui.h"
#include "mii.h"

#include <stdio.h>
#include <string.h>

extern uint8_t g_apple_fb[160 * 240];
extern mii_t   g_mii;
extern void    draw_string(uint8_t *fb, int fb_width, int x, int y, const char *str, uint8_t color);
extern void    apple_set_monitor_palette(uint8_t mode);
extern void    clear_held_key(void);

static inline void menu_put_pixel_4bpp(int x, int y, uint8_t color)
{
    uint32_t pix_idx = (uint32_t)y * 320u + (uint32_t)x;
    uint8_t *p = &g_apple_fb[pix_idx >> 1];
    if (pix_idx & 1u) {
        *p = (uint8_t)((*p & 0x0Fu) | ((color & 0x0Fu) << 4));
    } else {
        *p = (uint8_t)((*p & 0xF0u) | (color & 0x0Fu));
    }
}

static void menu_fill_rect_4bpp(int x, int y, int w, int h, uint8_t color)
{
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            menu_put_pixel_4bpp(x + dx, y + dy, color);
        }
    }
}

static bool    s_menuOpen = false;
static bool    s_menuDirty = false;
static int     s_menuSelection = 0;
static uint8_t s_monitorMode = 0; /* 0 = Color, 1 = Green, 2 = Amber */

static const char *const s_audioModeNames[] = {
    "HDMI Audio (48 kHz)",
    "I2S PCM5102 (GPIO 18/19/21)",
    "PWM Audio (GPIO 26)"
};

static const char *const s_monitorNames[] = {
    "Color NTSC",
    "Green Monochrome",
    "Amber Monochrome"
};

#define MENU_ITEM_COUNT 7

void apple_config_menu_open(void)
{
    s_menuOpen = true;
    s_menuDirty = true;
}

void apple_config_menu_close(void)
{
    s_menuOpen = false;
    s_menuDirty = false;
}

bool apple_config_menu_is_open(void)
{
    return s_menuOpen;
}

void apple_config_menu_render(void)
{
    if (!s_menuOpen || !s_menuDirty) return;
    s_menuDirty = false;

    const int wx = 20;
    const int wy = 32;
    const int ww = 280;
    const int wh = 166;

    /* Fond noir */
    menu_fill_rect_4bpp(wx, wy, ww, wh, 0);

    /* Bordure verte Apple II (couleur 12) */
    for (int x = wx; x < wx + ww; x++) {
        menu_put_pixel_4bpp(x, wy, 12);
        menu_put_pixel_4bpp(x, wy + wh - 1, 12);
    }
    for (int y = wy; y < wy + wh; y++) {
        menu_put_pixel_4bpp(wx, y, 12);
        menu_put_pixel_4bpp(wx + ww - 1, y, 12);
    }

    draw_string(g_apple_fb, 320, wx + 66, wy + 8, "APPLE IIe CONFIG", 15);

    char line[48];
    const int itemY = wy + 28;

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        switch (i) {
            case 0:
                snprintf(line, sizeof(line), "Audio : %s", s_audioModeNames[audioGetOutputMode() % 3u]);
                break;
            case 1:
                snprintf(line, sizeof(line), "Volume: %u%%", (unsigned)audioGetVolume());
                break;
            case 2:
                snprintf(line, sizeof(line), "Screen: %s", s_monitorNames[s_monitorMode % 3u]);
                break;
            case 3:
                snprintf(line, sizeof(line), "Browse Disks (.DSK/.WOZ/.NIB/.BDSK)");
                break;
            case 4:
                snprintf(line, sizeof(line), "Warm Reset (Ctrl+Reset)");
                break;
            case 5:
                snprintf(line, sizeof(line), "Cold Reset (Power Cycle)");
                break;
            case 6:
                snprintf(line, sizeof(line), "Exit Menu");
                break;
            default:
                line[0] = '\0';
                break;
        }

        if (s_menuSelection == i) {
            menu_fill_rect_4bpp(wx + 8, itemY + i * 14 - 2, ww - 16, 12, 12);
            draw_string(g_apple_fb, 320, wx + 12, itemY + i * 14, line, 0);
        } else {
            draw_string(g_apple_fb, 320, wx + 12, itemY + i * 14, line, 15);
        }
    }

    draw_string(g_apple_fb, 320, wx + 10, wy + wh - 16,
                "[UP/DN] Select  [L/R] Change  [ESC] Close", 13);
}

void apple_config_menu_key(uint8_t key)
{
    if (!s_menuOpen) return;
    s_menuDirty = true;

    if (key == 0x1B) { /* ESC */
        apple_config_menu_close();
        return;
    }

    if (key == 0x0B) { /* UP */
        s_menuSelection = (s_menuSelection > 0) ? (s_menuSelection - 1) : (MENU_ITEM_COUNT - 1);
    } else if (key == 0x0A) { /* DOWN */
        s_menuSelection = (s_menuSelection + 1) % MENU_ITEM_COUNT;
    } else if (key == 0x0D) { /* ENTER */
        if (s_menuSelection == 3) {
            apple_config_menu_close();
            disk_ui_show();
        } else if (s_menuSelection == 4) {
            clear_held_key();
            mii_reset(&g_mii, false);
            apple_config_menu_close();
        } else if (s_menuSelection == 5) {
            clear_held_key();
            mii_reset(&g_mii, true);
            apple_config_menu_close();
        } else if (s_menuSelection == 6) {
            apple_config_menu_close();
        }
    } else if (key == 0x15) { /* RIGHT */
        if (s_menuSelection == 0) {
            audioSetOutputMode((uint8_t)((audioGetOutputMode() + 1u) % 3u));
        } else if (s_menuSelection == 1) {
            uint8_t v = audioGetVolume();
            if (v < 100u) audioSetVolume((uint8_t)(v + 10u));
        } else if (s_menuSelection == 2) {
            s_monitorMode = (uint8_t)((s_monitorMode + 1u) % 3u);
            apple_set_monitor_palette(s_monitorMode);
        }
    } else if (key == 0x08) { /* LEFT */
        if (s_menuSelection == 0) {
            audioSetOutputMode((uint8_t)((audioGetOutputMode() + 2u) % 3u));
        } else if (s_menuSelection == 1) {
            uint8_t v = audioGetVolume();
            if (v > 10u) audioSetVolume((uint8_t)(v - 10u));
        } else if (s_menuSelection == 2) {
            s_monitorMode = (uint8_t)((s_monitorMode + 2u) % 3u);
            apple_set_monitor_palette(s_monitorMode);
        }
    }
}
