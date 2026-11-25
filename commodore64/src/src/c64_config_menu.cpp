/*
 * c64_config_menu.cpp - Menu de Configuration OSD C64 pour Waveshare RP2350-PiZero
 * Accessible via F4 / F12 ou Select+Start sur manette NES.
 */

#include "c64_config_menu.h"
#include "board_config.h"
#include "audio.h"
#include "hdmi_dvi.h"
#include "disk_ui.h"
#include <stdio.h>
#include <string.h>

extern "C" {
int input_get_joy_port(void);
void c64_reset(void);
void c64_nmi(void);
bool c64_get_1541_proc(void);
void c64_set_1541_proc(bool enable);
}

extern uint8_t g_pixels[384 * 272];

static bool s_menuOpen = false;
static bool s_menuDirty = false;
static int  s_menuSelection = 0;

static const char *s_audioModeNames[] = {
    "HDMI Audio (48 kHz L-PCM)",
    "I2S PCM5102 (GPIO 18/19/21)",
    "PWM Audio (GPIO 26)"
};

static uint8_t s_paletteType = 0; // 0 = Pepto, 1 = Colodore
static int     s_joyPort = 2;

// Compact font
extern const uint8_t font_6x8[][8];

static void draw_text(int x, int y, const char *str, uint8_t color, uint8_t bg)
{
    uint8_t *fb = g_pixels;
    int cur_x = x;
    while (*str) {
        char ch = *str++;
        if (ch < 32 || ch > 126) ch = ' ';
        const uint8_t *glyph = font_6x8[(unsigned char)ch - 32];
        for (int r = 0; r < 8; r++) {
            uint8_t bits = glyph[r];
            uint8_t *dst = fb + (y + r + C64_CROP_TOP) * C64_DISPLAY_WIDTH + cur_x + C64_CROP_LEFT;
            for (int c = 0; c < 6; c++) {
                dst[c] = (bits & (0x80 >> c)) ? color : bg;
            }
        }
        cur_x += 6;
    }
}

void c64_config_menu_open(void)
{
    s_menuOpen = true;
    s_menuDirty = true;
    s_joyPort = input_get_joy_port();
}

void c64_config_menu_close(void)
{
    s_menuOpen = false;
    s_menuDirty = false;
}

bool c64_config_menu_is_open(void)
{
    return s_menuOpen;
}

void c64_config_menu_render(void)
{
    if (!s_menuOpen || !s_menuDirty) return;
    s_menuDirty = false;

    // Fenêtre centrée 260x182
    int wx = 30;
    int wy = 29;
    int ww = 260;
    int wh = 182;

    uint8_t *fb = g_pixels;
    for (int y = wy; y < wy + wh; y++) {
        uint8_t *row = fb + (y + C64_CROP_TOP) * C64_DISPLAY_WIDTH + wx + C64_CROP_LEFT;
        memset(row, 0, ww); // Noir
    }

    // Bordure bleue claire (14)
    for (int x = wx; x < wx + ww; x++) {
        fb[(wy + C64_CROP_TOP) * C64_DISPLAY_WIDTH + x + C64_CROP_LEFT] = 14;
        fb[(wy + wh - 1 + C64_CROP_TOP) * C64_DISPLAY_WIDTH + x + C64_CROP_LEFT] = 14;
    }
    for (int y = wy; y < wy + wh; y++) {
        fb[(y + C64_CROP_TOP) * C64_DISPLAY_WIDTH + wx + C64_CROP_LEFT] = 14;
        fb[(y + C64_CROP_TOP) * C64_DISPLAY_WIDTH + wx + ww - 1 + C64_CROP_LEFT] = 14;
    }

    draw_text(wx + 45, wy + 8, "COMMODORE 64 CONFIG", 1, 0);

    // Items de menu
    char line[64];
    const int itemY = wy + 30;

    // 0: Sortie Audio
    snprintf(line, sizeof(line), "Audio Output: %s", s_audioModeNames[audioGetOutputMode()]);
    draw_text(wx + 15, itemY + 0 * 14, line, (s_menuSelection == 0) ? 0 : 1, (s_menuSelection == 0) ? 14 : 0);

    // 1: Volume
    snprintf(line, sizeof(line), "Volume: %d%%", audioGetVolume());
    draw_text(wx + 15, itemY + 1 * 14, line, (s_menuSelection == 1) ? 0 : 1, (s_menuSelection == 1) ? 14 : 0);

    // 2: Joystick Port
    snprintf(line, sizeof(line), "Joystick Port: Port %d", s_joyPort);
    draw_text(wx + 15, itemY + 2 * 14, line, (s_menuSelection == 2) ? 0 : 1, (s_menuSelection == 2) ? 14 : 0);

    // 3: Palette
    snprintf(line, sizeof(line), "Color Palette: %s", (s_paletteType == 0) ? "Pepto PAL" : "Colodore");
    draw_text(wx + 15, itemY + 3 * 14, line, (s_menuSelection == 3) ? 0 : 1, (s_menuSelection == 3) ? 14 : 0);

    // 4: 1541 Drive Emulation
    snprintf(line, sizeof(line), "1541 Drive: %s", c64_get_1541_proc() ? "True 1541 (Full)" : "Fast IEC (DOS)");
    draw_text(wx + 15, itemY + 4 * 14, line, (s_menuSelection == 4) ? 0 : 1, (s_menuSelection == 4) ? 14 : 0);

    // 5: Disk Menu
    draw_text(wx + 15, itemY + 5 * 14, "Browse MicroSD Disks (.D64 / .PRG)", (s_menuSelection == 5) ? 0 : 1, (s_menuSelection == 5) ? 14 : 0);

    // 6: Reset C64
    draw_text(wx + 15, itemY + 6 * 14, "Reset Commodore 64", (s_menuSelection == 6) ? 0 : 1, (s_menuSelection == 6) ? 14 : 0);

    // 7: RESTORE (NMI)
    draw_text(wx + 15, itemY + 7 * 14, "Trigger RESTORE Key (NMI)", (s_menuSelection == 7) ? 0 : 1, (s_menuSelection == 7) ? 14 : 0);

    // 8: Quitter menu
    draw_text(wx + 15, itemY + 8 * 14, "Exit Menu", (s_menuSelection == 8) ? 0 : 1, (s_menuSelection == 8) ? 14 : 0);

    draw_text(wx + 20, wy + wh - 16, "[UP/DN] Select  [LEFT/RIGHT] Change  [ESC] Close", 7, 0);
}

void c64_config_menu_key(uint8_t key)
{
    if (!s_menuOpen) return;
    s_menuDirty = true;

    if (key == 0x1B) { // ESC
        c64_config_menu_close();
        return;
    }

    if (key == 0x0B || key == 0x52) { // UP
        s_menuSelection = (s_menuSelection > 0) ? s_menuSelection - 1 : 8;
    } else if (key == 0x0A || key == 0x51) { // DOWN
        s_menuSelection = (s_menuSelection < 8) ? s_menuSelection + 1 : 0;
    } else if (key == 0x0D) { // ENTER
        if (s_menuSelection == 5) {
            c64_config_menu_close();
            disk_ui_show();
        } else if (s_menuSelection == 6) {
            c64_reset();
            c64_config_menu_close();
        } else if (s_menuSelection == 7) {
            c64_nmi();
            c64_config_menu_close();
        } else if (s_menuSelection == 8) {
            c64_config_menu_close();
        }
    } else if (key == 0x15 || key == 0x4F) { // RIGHT
        if (s_menuSelection == 0) {
            uint8_t m = (audioGetOutputMode() + 1u) % 3u;
            audioSetOutputMode(m);
        } else if (s_menuSelection == 1) {
            uint8_t v = audioGetVolume();
            if (v < 100) audioSetVolume(v + 10);
        } else if (s_menuSelection == 2) {
            s_joyPort = (s_joyPort == 1) ? 2 : 1;
        } else if (s_menuSelection == 3) {
            s_paletteType ^= 1u;
            graphics_restore_c64_palette(s_paletteType);
        } else if (s_menuSelection == 4) {
            c64_set_1541_proc(!c64_get_1541_proc());
        }
    } else if (key == 0x08 || key == 0x50) { // LEFT
        if (s_menuSelection == 0) {
            uint8_t m = (audioGetOutputMode() + 2u) % 3u;
            audioSetOutputMode(m);
        } else if (s_menuSelection == 1) {
            uint8_t v = audioGetVolume();
            if (v > 10) audioSetVolume(v - 10);
        } else if (s_menuSelection == 2) {
            s_joyPort = (s_joyPort == 1) ? 2 : 1;
        } else if (s_menuSelection == 3) {
            s_paletteType ^= 1u;
            graphics_restore_c64_palette(s_paletteType);
        } else if (s_menuSelection == 4) {
            c64_set_1541_proc(!c64_get_1541_proc());
        }
    }
}
