/*
 * input_rp2350.cpp - Gestion unifiée des entrées C64 pour Waveshare RP2350-PiZero
 * Clavier USB Dual-Host (rhport0 natif + rhport1 PIO-USB), Clavier PS/2 (PIO2),
 * Manette NES (GPIO 5/6/9/10), Menu OSD Configuration (F4, F12), Sélecteur MicroSD (F11).
 */

#include "board_config.h"
#include "usb_kbd.h"
#include "ps2_kbd.h"
#include "nes_ctrl.h"
#include "c64_config_menu.h"
#include "disk_ui.h"

#include "pico/stdlib.h"
#include <cstring>
#include <cstdio>

#define MATRIX(row, col) (((row) << 3) | (col))

/*
 * C64 keyboard matrix:
 *
 *     Bit 7   6   5   4   3   2   1   0
 *   0    CUD  F5  F3  F1  F7 CLR RET DEL
 *   1    SHL  E   S   Z   4   A   W   3
 *   2     X   T   F   C   6   D   R   5
 *   3     V   U   H   B   8   G   Y   7
 *   4     N   O   K   M   0   J   I   9
 *   5     ,   @   :   .   -   L   P   +
 *   6     /   ↑   =  SHR HOM  ;   *   £
 *   7    R/S  Q   C= SPC  2  CTL  ←   1
 */

// PS/2 Set 2 scancodes -> C64 matrix position (vérifié QWERTY)
static const int8_t ps2_to_c64[128] = {
    -1,            -1,            -1,            -1,            // 00-03
    -1,            MATRIX(0, 4),  MATRIX(0, 4),  -1,            // 04-07: -, F1, F2, F12
    -1,            -1,            MATRIX(0, 3),  -1,            // 08-0B: -, F10, F8, F6
    -1,            MATRIX(7, 2),  MATRIX(7, 1),  -1,            // 0C-0F: -, Tab(CTRL), `(<-), -
    -1,            MATRIX(7, 5),  MATRIX(1, 7),  -1,            // 10-13: -, LAlt(C=), LShift, -
    MATRIX(7, 2),  MATRIX(7, 6),  MATRIX(7, 0),  -1,            // 14-17: LCtrl, Q, 1, -
    -1,            -1,            MATRIX(1, 4),  MATRIX(1, 5),  // 18-1B: -, -, Z, S
    MATRIX(1, 2),  MATRIX(1, 1),  MATRIX(7, 3),  -1,            // 1C-1F: A, W, 2, -
    -1,            MATRIX(2, 4),  MATRIX(2, 7),  MATRIX(2, 2),  // 20-23: -, C, X, D
    MATRIX(1, 6),  MATRIX(1, 3),  MATRIX(1, 0),  -1,            // 24-27: E, 4, 3, -
    -1,            MATRIX(7, 4),  MATRIX(3, 7),  MATRIX(2, 5),  // 28-2B: -, Space, V, F
    MATRIX(2, 6),  MATRIX(2, 1),  MATRIX(2, 0),  -1,            // 2C-2F: T, R, 5, -
    -1,            MATRIX(4, 7),  MATRIX(3, 4),  MATRIX(3, 5),  // 30-33: -, N, B, H
    MATRIX(3, 2),  MATRIX(3, 1),  MATRIX(2, 3),  -1,            // 34-37: G, Y, 6, -
    -1,            -1,            MATRIX(4, 4),  MATRIX(4, 2),  // 38-3B: -, -, M, J
    MATRIX(3, 6),  MATRIX(3, 0),  MATRIX(3, 3),  -1,            // 3C-3F: U, 7, 8, -
    -1,            MATRIX(5, 7),  MATRIX(4, 5),  MATRIX(4, 1),  // 40-43: -, ,, K, I
    MATRIX(4, 6),  MATRIX(4, 3),  MATRIX(4, 0),  -1,            // 44-47: O, 0, 9, -
    -1,            MATRIX(5, 4),  MATRIX(6, 7),  MATRIX(5, 2),  // 48-4B: -, ., /, L
    MATRIX(5, 5),  MATRIX(5, 1),  MATRIX(5, 3),  -1,            // 4C-4F: ;, P, -, -
    -1,            -1,            MATRIX(6, 2),  -1,            // 50-53: -, -, ', -
    MATRIX(5, 6),  MATRIX(5, 0),  -1,            -1,            // 54-57: [, =, -, -
    -1,            MATRIX(6, 4),  MATRIX(0, 1),  MATRIX(6, 1),  // 58-5B: -, RShift, Enter, ]
    -1,            MATRIX(6, 0),  -1,            -1,            // 5C-5F: -, \, -, -
    -1,            -1,            -1,            -1,            // 60-63: -
    -1,            -1,            MATRIX(0, 0),  -1,            // 64-67: -, -, Backspace(DEL), -
    -1,            -1,            -1,            -1,            // 68-6B: -
    -1,            -1,            -1,            -1,            // 6C-6F: -
    -1,            -1,            -1,            -1,            // 70-73: -
    -1,            -1,            MATRIX(7, 7),  -1,            // 74-77: -, -, Esc(RUN/STOP), -
    -1,            -1,            -1,            -1,            // 78-7B: -
    -1,            -1,            -1,            -1             // 7C-7F: -
};

static inline int8_t get_extended_c64_key(uint8_t scancode) {
    switch (scancode) {
        case 0x11: return MATRIX(7, 5);  // R-Alt -> C=
        case 0x14: return MATRIX(7, 2);  // R-Ctrl
        case 0x6B: return MATRIX(0, 2);  // Left arrow
        case 0x72: return MATRIX(0, 7);  // Down arrow
        case 0x74: return MATRIX(0, 2);  // Right arrow
        case 0x75: return MATRIX(0, 7);  // Up arrow
        case 0x6C: return MATRIX(6, 3);  // Home -> CLR/HOME
        case 0x69: return MATRIX(6, 0);  // End -> £
        case 0x7D: return MATRIX(6, 6);  // Page Up -> ↑
        case 0x7A: return MATRIX(6, 5);  // Page Down -> =
        case 0x71: return MATRIX(0, 0);  // Delete -> INS/DEL
        default: return -1;
    }
}

static struct {
    uint8_t key_matrix[8];
    uint8_t rev_matrix[8];
    uint8_t joystick1;
    uint8_t joystick2;
    int     joy_port;
    bool    shift_lock;
    bool    phys_lshift;
    bool    virt_shift_left;
    bool    virt_shift_up;
} s_input;

static Ps2Decoder s_ps2Dec;

extern "C" {
void c64_reset(void);
void c64_nmi(void);
void c64_unmount_disk(void);
void c64_eject_cartridge(void);
void c64_load_file(const char *filename);
void c64_mount_disk(const uint8_t *data, uint32_t size, const char *filename);

const char *disk_loader_get_path(int index);

void input_reset_keys(void)
{
    memset(s_input.key_matrix, 0xFF, sizeof(s_input.key_matrix));
    memset(s_input.rev_matrix, 0xFF, sizeof(s_input.rev_matrix));
    s_input.joystick1 = 0xFF;
    s_input.joystick2 = 0xFF;
    s_input.phys_lshift = false;
    s_input.virt_shift_left = false;
    s_input.virt_shift_up = false;
}

void input_rp2350_init(void)
{
    input_reset_keys();
    s_input.joystick1 = 0xFF;
    s_input.joystick2 = 0xFF;
    s_input.joy_port = 2; // Port 2 par defaut
    s_input.shift_lock = false;

    ps2_decoder_reset(&s_ps2Dec);
    ps2kbd_begin();
    usb_kbd_init();
    nes_begin();
    disk_ui_init();
}

static void apply_c64_key(int key, bool pressed)
{
    if (key < 0) return;
    int row = (key >> 3) & 7;
    int col = key & 7;

    if (pressed) {
        s_input.key_matrix[row] &= ~(1u << col);
        s_input.rev_matrix[col] &= ~(1u << row);
    } else {
        s_input.key_matrix[row] |= (1u << col);
        s_input.rev_matrix[col] |= (1u << row);
    }
}

static void handle_key_event(const Ps2KeyEvent *ev)
{
    uint8_t code = ev->code;
    bool pressed = ev->pressed;

    // F4 ou F12 : Menu de Configuration OSD
    if ((code == 0x0C || code == 0x07) && !ev->extended) { // F4=0x0C, F12=0x07
        if (pressed) {
            input_reset_keys();
            if (c64_config_menu_is_open()) c64_config_menu_close();
            else c64_config_menu_open();
        }
        return;
    }

    // F11 : Sélecteur de Disquettes MicroSD
    if (code == 0x78 && !ev->extended) { // F11
        if (pressed) {
            input_reset_keys();
            if (disk_ui_is_visible()) disk_ui_hide();
            else disk_ui_show();
        }
        return;
    }

    // F9 : Inverser Port Joystick 1 / 2
    if (code == 0x01 && !ev->extended) { // F9
        if (pressed) {
            s_input.joy_port = (s_input.joy_port == 1) ? 2 : 1;
        }
        return;
    }

    // Si le Menu Configuration OSD est actif
    if (c64_config_menu_is_open()) {
        if (pressed) {
            if (ev->extended) {
                if (code == 0x75) c64_config_menu_key(0x0B); // UP
                else if (code == 0x72) c64_config_menu_key(0x0A); // DOWN
                else if (code == 0x6B) c64_config_menu_key(0x08); // LEFT
                else if (code == 0x74) c64_config_menu_key(0x15); // RIGHT
            } else {
                if (code == 0x5A) c64_config_menu_key(0x0D); // ENTER
                else if (code == 0x76) { input_reset_keys(); c64_config_menu_close(); } // ESC
            }
        }
        return;
    }

    // Si le Sélecteur de Disquettes est actif
    if (disk_ui_is_visible()) {
        if (pressed) {
            int state = disk_ui_get_state();
            if (state == DISK_UI_SELECT_FILE) {
                if (ev->extended) {
                    if (code == 0x75) disk_ui_move_up();
                    else if (code == 0x72) disk_ui_move_down();
                    else if (code == 0x6C) disk_ui_home();
                    else if (code == 0x69) disk_ui_end();
                    else if (code == 0x7D) disk_ui_page_up();
                    else if (code == 0x7A) disk_ui_page_down();
                } else {
                    if (code == 0x5A) disk_ui_select();
                    else if (code == 0x76) { input_reset_keys(); disk_ui_hide(); }
                }
            } else if (state == DISK_UI_SELECT_ACTION) {
                if (ev->extended) {
                    if (code == 0x75) disk_ui_action_up();
                    else if (code == 0x72) disk_ui_action_down();
                } else {
                    if (code == 0x5A) {
                        int sel = disk_ui_get_selected();
                        int action = disk_ui_get_action();
                        const char *path = disk_loader_get_path(sel);
                        if (path) {
                            if (action == 0) {
                                c64_eject_cartridge();
                                c64_load_file(path);
                            } else {
                                c64_mount_disk(NULL, 0, path);
                            }
                            disk_ui_confirm_action();
                            input_reset_keys();
                        }
                    } else if (code == 0x76) {
                        disk_ui_cancel_action();
                    }
                }
            }
        }
        return;
    }

    // Touches C64 normales
    if (ev->extended) {
        if (code == 0x6B) { // Fleche Gauche -> L-Shift + CRSR Droite/Gauche
            s_input.virt_shift_left = pressed;
            apply_c64_key(MATRIX(1, 7), s_input.phys_lshift || s_input.virt_shift_left || s_input.virt_shift_up);
            apply_c64_key(MATRIX(0, 2), pressed);
        } else if (code == 0x75) { // Fleche Haut -> L-Shift + CRSR Bas/Haut
            s_input.virt_shift_up = pressed;
            apply_c64_key(MATRIX(1, 7), s_input.phys_lshift || s_input.virt_shift_left || s_input.virt_shift_up);
            apply_c64_key(MATRIX(0, 7), pressed);
        } else {
            int k = get_extended_c64_key(code);
            if (k >= 0) {
                apply_c64_key(k, pressed);
            }
        }
    } else if (code < 128) {
        if (code == 0x12) { // L-Shift physique
            s_input.phys_lshift = pressed;
            apply_c64_key(MATRIX(1, 7), s_input.phys_lshift || s_input.virt_shift_left || s_input.virt_shift_up);
        } else {
            int k = ps2_to_c64[code];
            if (k >= 0) {
                apply_c64_key(k, pressed);
            }
        }
    }
}

void input_rp2350_poll_no_c64_acts(void)
{
    // 1. USB Keyboard poll
    usb_kbd_poll();
    Ps2KeyEvent ev;
    while (usb_kbd_pop_event(&ev)) {
        handle_key_event(&ev);
    }

    // 2. PS/2 Keyboard poll
    uint8_t raw = 0;
    while ((raw = ps2kbd_read()) != 0) {
        Ps2KeyEvent ev;
        if (ps2_decoder_feed(&s_ps2Dec, raw, &ev)) {
            handle_key_event(&ev);
        }
    }
    while (ps2_queue_pop(&raw)) {
        Ps2KeyEvent ev;
        if (ps2_decoder_feed(&s_ps2Dec, raw, &ev)) {
            handle_key_event(&ev);
        }
    }

    // 3. NES Gamepad poll
    uint8_t nes_raw = 0xFF;
    uint8_t joy = 0xFF;
    if (nes_poll_nonblocking(&nes_raw) && (nes_raw != 0xFF)) {
        // Combinaison Select + Start : OSD Config Menu
        static bool s_prevMenuCombo = false;
        bool menu_combo = ((nes_raw & 0x30) == 0);
        if (menu_combo && !s_prevMenuCombo) {
            input_reset_keys();
            if (c64_config_menu_is_open()) c64_config_menu_close();
            else c64_config_menu_open();
        }
        s_prevMenuCombo = menu_combo;

        // C64 joystick format (active-bas) :
        // Up=0x01, Down=0x02, Left=0x04, Right=0x08, Fire=0x10
        if (!(nes_raw & 0x08)) joy &= ~0x01u; // Up
        if (!(nes_raw & 0x04)) joy &= ~0x02u; // Down
        if (!(nes_raw & 0x02)) joy &= ~0x04u; // Left
        if (!(nes_raw & 0x01)) joy &= ~0x08u; // Right
        if (!(nes_raw & 0xC0)) joy &= ~0x10u; // Fire (A ou B)
    }

    if (s_input.joy_port == 2) {
        s_input.joystick2 = joy;
        s_input.joystick1 = 0xFF;
    } else {
        s_input.joystick1 = joy;
        s_input.joystick2 = 0xFF;
    }
}

void input_rp2350_poll(uint8_t *key_matrix, uint8_t *rev_matrix, uint8_t *joystick)
{
    input_rp2350_poll_no_c64_acts();
    memcpy(key_matrix, s_input.key_matrix, 8);
    memcpy(rev_matrix, s_input.rev_matrix, 8);
    *joystick = s_input.joystick1;
}

uint8_t input_get_joystick2(void)
{
    return s_input.joystick2;
}

int input_get_joy_port(void)
{
    return s_input.joy_port;
}

} // extern "C"
