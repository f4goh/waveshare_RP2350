/*
 * input_rp2350.cpp - Entrees Apple IIe pour Waveshare RP2350-PiZero
 * Clavier USB Dual-Host (natif + PIO-USB, hubs), Clavier PS/2 (PIO2),
 * Manette NES (GPIO 5/6/9), Menu OSD (F4/F12) & Selecteur Disquettes (F11).
 */

#include "board_config.h"
#include "usb_kbd.h"
#include "ps2_kbd.h"
#include "nes_ctrl.h"
#include "apple_config_menu.h"

extern "C" {
#include "disk_ui.h"
void apple_keypress(uint8_t key);
void apple_reset_req(bool cold);
void apple_key_repeat_tick(uint8_t key);
void apple_set_paddles(uint8_t x, uint8_t y, bool btn0, bool btn1, bool btn2);
}

#include <cstring>

static Ps2Decoder s_ps2Dec;
static bool s_shiftHeld   = false;
static bool s_ctrlHeld    = false;
static bool s_capsLock    = false;
static bool s_leftAlt     = false;
static bool s_rightAlt    = false;
static bool s_leftWin     = false;
static bool s_rightWin    = false;
static bool s_showSpeed   = false;
static bool s_fastHold    = false;
static bool s_fastToggle  = false;

static bool s_kpUp    = false;
static bool s_kpDown2 = false;
static bool s_kpDown5 = false;
static bool s_kpLeft  = false;
static bool s_kpRight = false;
static bool s_kpIns   = false;
static bool s_kpDel   = false;
static bool s_arrUp   = false;
static bool s_arrDown = false;
static bool s_arrLeft = false;
static bool s_arrRight = false;

static uint8_t  s_heldKey = 0;
static uint32_t s_heldFrames = 0;
#define KEY_REPEAT_INITIAL_DELAY 30
#define KEY_REPEAT_RATE          4

/* Table PS/2 Set-2 -> ASCII minuscule / majuscule (QWERTY US) */
static char ps2_set2_to_char(uint8_t code, bool shift)
{
    switch (code) {
        case 0x0E: return shift ? '~' : '`';
        case 0x16: return shift ? '!' : '1';
        case 0x1E: return shift ? '@' : '2';
        case 0x26: return shift ? '#' : '3';
        case 0x25: return shift ? '$' : '4';
        case 0x2E: return shift ? '%' : '5';
        case 0x36: return shift ? '^' : '6';
        case 0x3D: return shift ? '&' : '7';
        case 0x3E: return shift ? '*' : '8';
        case 0x46: return shift ? '(' : '9';
        case 0x45: return shift ? ')' : '0';
        case 0x4E: return shift ? '_' : '-';
        case 0x55: return shift ? '+' : '=';
        case 0x15: return shift ? 'Q' : 'q';
        case 0x1D: return shift ? 'W' : 'w';
        case 0x24: return shift ? 'E' : 'e';
        case 0x2D: return shift ? 'R' : 'r';
        case 0x2C: return shift ? 'T' : 't';
        case 0x35: return shift ? 'Y' : 'y';
        case 0x3C: return shift ? 'U' : 'u';
        case 0x43: return shift ? 'I' : 'i';
        case 0x44: return shift ? 'O' : 'o';
        case 0x4D: return shift ? 'P' : 'p';
        case 0x54: return shift ? '{' : '[';
        case 0x5B: return shift ? '}' : ']';
        case 0x5D: return shift ? '|' : '\\';
        case 0x1C: return shift ? 'A' : 'a';
        case 0x1B: return shift ? 'S' : 's';
        case 0x23: return shift ? 'D' : 'd';
        case 0x2B: return shift ? 'F' : 'f';
        case 0x34: return shift ? 'G' : 'g';
        case 0x33: return shift ? 'H' : 'h';
        case 0x3B: return shift ? 'J' : 'j';
        case 0x42: return shift ? 'K' : 'k';
        case 0x4B: return shift ? 'L' : 'l';
        case 0x4C: return shift ? ':' : ';';
        case 0x52: return shift ? '"' : '\'';
        case 0x1A: return shift ? 'Z' : 'z';
        case 0x22: return shift ? 'X' : 'x';
        case 0x21: return shift ? 'C' : 'c';
        case 0x2A: return shift ? 'V' : 'v';
        case 0x32: return shift ? 'B' : 'b';
        case 0x31: return shift ? 'N' : 'n';
        case 0x3A: return shift ? 'M' : 'm';
        case 0x41: return shift ? '<' : ',';
        case 0x49: return shift ? '>' : '.';
        case 0x4A: return shift ? '?' : '/';
        case 0x29: return ' ';
        case 0x5A: return '\r';
        case 0x66: return 0x08;
        case 0x76: return 0x1B;
        case 0x0D: return '\t';
        case 0x70: return '0';
        case 0x69: return '1';
        case 0x72: return '2';
        case 0x7A: return '3';
        case 0x6B: return '4';
        case 0x73: return '5';
        case 0x74: return '6';
        case 0x6C: return '7';
        case 0x75: return '8';
        case 0x7D: return '9';
        case 0x7C: return '*';
        case 0x7B: return '-';
        case 0x79: return '+';
        case 0x71: return '.';
        default:   return 0;
    }
}

static uint8_t translate_to_apple_key(const Ps2KeyEvent *ev)
{
    uint8_t code = ev->code;
    if (ev->extended) {
        switch (code) {
            case 0x6B: return 0x08; // Left
            case 0x74: return 0x15; // Right
            case 0x72: return 0x0A; // Down
            case 0x75: return 0x0B; // Up
            case 0x71: return 0x7F; // Delete
            case 0x5A: return '\r'; // Numpad Enter
            case 0x4A: return '/';  // Numpad /
            default:   return 0;
        }
    }

    if (code >= 128) return 0;
    char ch = ps2_set2_to_char(code, s_shiftHeld);
    if (!ch) return 0;

    if (s_ctrlHeld) {
        if (ch >= 'a' && ch <= 'z') return (uint8_t)(ch - 'a' + 1);
        if (ch >= 'A' && ch <= 'Z') return (uint8_t)(ch - 'A' + 1);
        if (ch == '[') return 0x1B;
    }

    if (s_capsLock && !s_shiftHeld && ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    return (uint8_t)ch;
}

static void handle_key_event(const Ps2KeyEvent *ev)
{
    uint8_t code = ev->code;
    bool pressed = ev->pressed;

    /* Modificateurs */
    if (!ev->extended && (code == 0x12 || code == 0x59)) {
        s_shiftHeld = pressed;
        return;
    }
    if (code == 0x14) {
        s_ctrlHeld = pressed;
        return;
    }
    if (!ev->extended && code == 0x58) {
        if (pressed) s_capsLock = !s_capsLock;
        return;
    }
    if (code == 0x11) {
        if (ev->extended) s_rightAlt = pressed;
        else              s_leftAlt  = pressed;
        return;
    }
    if (ev->extended && code == 0x1F) { /* Left Win -> Open Apple */
        s_leftWin = pressed;
        return;
    }
    if (ev->extended && code == 0x27) { /* Right Win -> Closed Apple */
        s_rightWin = pressed;
        return;
    }

    /* Ctrl + Alt + Delete : Warm Reset */
    if (ev->extended && code == 0x71 && pressed &&
        s_ctrlHeld && (s_leftAlt || s_rightAlt)) {
        s_heldKey = 0;
        apple_reset_req(false);
        return;
    }

    /* Touches speciales systeme */
    if (!ev->extended) {
        if (code == 0x07) { /* F12 : deblocage vitesse CPU (maintien) */
            s_fastHold = pressed;
            return;
        }
        if (pressed) {
            if (code == 0x0C) { /* F4 : Menu Config */
                s_heldKey = 0;
                if (apple_config_menu_is_open()) apple_config_menu_close();
                else apple_config_menu_open();
                return;
            }
            if (code == 0x01) { /* F9 : afficher vitesse CPU (kHz + %) */
                s_showSpeed = !s_showSpeed;
                return;
            }
            if (code == 0x78) { /* F11 : Navigateur disquettes */
                s_heldKey = 0;
                disk_ui_toggle();
                return;
            }
            if (code == 0x7E) { /* ScrLock : bascule deblocage vitesse CPU */
                s_fastToggle = !s_fastToggle;
                return;
            }
            if (code == 0x06) { /* F2 : Reset Apple IIe (Ctrl+F2 = Cold Reset) */
                s_heldKey = 0;
                apple_reset_req(s_ctrlHeld);
                return;
            }
        }
    }

    /* Mapping Clavier -> Joystick (KP 2/4/5/6/8, Ins=START, Del=SELECT) */
    if (!apple_config_menu_is_open() && !disk_ui_is_visible()) {
        if (!ev->extended) {
            switch (code) {
                case 0x75: s_kpUp    = pressed; return; /* KP 8 -> UP */
                case 0x72: s_kpDown2 = pressed; return; /* KP 2 -> DOWN */
                case 0x73: s_kpDown5 = pressed; return; /* KP 5 -> DOWN */
                case 0x6B: s_kpLeft  = pressed; return; /* KP 4 -> LEFT */
                case 0x74: s_kpRight = pressed; return; /* KP 6 -> RIGHT */
                default: break;
            }
        } else {
            if (code == 0x70) { s_kpIns = pressed; return; } /* Ins -> START */
            if (code == 0x71) { s_kpDel = pressed; return; } /* Del -> SELECT */
            /* Fleches classiques -> Joystick en plus des touches curseurs Apple II */
            switch (code) {
                case 0x75: s_arrUp    = pressed; break;
                case 0x72: s_arrDown  = pressed; break;
                case 0x6B: s_arrLeft  = pressed; break;
                case 0x74: s_arrRight = pressed; break;
                default: break;
            }
        }
    }

    uint8_t apple_key = translate_to_apple_key(ev);
    if (!apple_key) {
        if (!pressed && s_heldKey != 0) {
            s_heldKey = 0;
            s_heldFrames = 0;
        }
        return;
    }

    if (apple_config_menu_is_open()) {
        if (pressed) {
            apple_config_menu_key(apple_key);
        }
        return;
    }

    if (disk_ui_is_visible()) {
        if (pressed) {
            disk_ui_handle_key(apple_key);
            s_heldKey = apple_key;
            s_heldFrames = 0;
        } else if (s_heldKey == apple_key) {
            s_heldKey = 0;
            s_heldFrames = 0;
        }
        return;
    }

    if (pressed) {
        apple_keypress(apple_key);
        s_heldKey = apple_key;
        s_heldFrames = 0;
    } else if (s_heldKey == apple_key) {
        s_heldKey = 0;
        s_heldFrames = 0;
    }
}

extern "C" {

uint8_t get_held_key(void)
{
    return s_heldKey;
}

bool input_is_show_speed(void)
{
    return s_showSpeed;
}

bool input_is_speed_unlocked(void)
{
    return s_fastHold || s_fastToggle;
}

void clear_held_key(void)
{
    s_heldKey = 0;
    s_heldFrames = 0;
    s_shiftHeld = false;
    s_ctrlHeld = false;
    s_leftAlt = false;
    s_rightAlt = false;
    s_leftWin = false;
    s_rightWin = false;
    s_kpUp = s_kpDown2 = s_kpDown5 = s_kpLeft = s_kpRight = s_kpIns = s_kpDel = false;
    s_arrUp = s_arrDown = s_arrLeft = s_arrRight = false;
}

void input_rp2350_init(void)
{
    clear_held_key();
    ps2_decoder_reset(&s_ps2Dec);
    ps2kbd_begin();
    usb_kbd_init();
    nes_begin();
}

void input_rp2350_poll_frame(void)
{
    /* 1. Clavier USB Dual-Host */
    usb_kbd_poll();
    Ps2KeyEvent ev;
    while (usb_kbd_pop_event(&ev)) {
        handle_key_event(&ev);
    }

    /* 2. Clavier PS/2 */
    uint8_t raw = 0;
    while ((raw = ps2kbd_read()) != 0) {
        if (ps2_decoder_feed(&s_ps2Dec, raw, &ev)) {
            handle_key_event(&ev);
        }
    }
    while (ps2_queue_pop(&raw)) {
        if (ps2_decoder_feed(&s_ps2Dec, raw, &ev)) {
            handle_key_event(&ev);
        }
    }

    /* 3. Repetition clavier Apple IIe */
    if (s_heldKey != 0) {
        s_heldFrames++;
        if (s_heldFrames >= KEY_REPEAT_INITIAL_DELAY) {
            uint32_t d = s_heldFrames - KEY_REPEAT_INITIAL_DELAY;
            if ((d % KEY_REPEAT_RATE) == 0) {
                if (disk_ui_is_visible()) {
                    disk_ui_handle_key(s_heldKey);
                } else if (!apple_config_menu_is_open()) {
                    apple_key_repeat_tick(s_heldKey);
                }
            }
        }
    }

    /* 4. Manettes (NES GPIO 5/6/9 + USB HID Gamepad + Clavier KP) */
    uint8_t nes_raw = 0xFF;
    static uint8_t s_lastNes = 0xFF;
    if (nes_poll_nonblocking(&nes_raw)) {
        s_lastNes = nes_raw;
    }

    uint8_t pad = (uint8_t)(~s_lastNes & 0xFFu);
    pad |= usb_gamepad_get_state();

    if (s_kpRight || s_arrRight)             pad |= 0x01u;
    if (s_kpLeft  || s_arrLeft)              pad |= 0x02u;
    if (s_kpDown2 || s_kpDown5 || s_arrDown) pad |= 0x04u;
    if (s_kpUp    || s_arrUp)                pad |= 0x08u;
    if (s_kpIns)               pad |= 0x10u; /* START */
    if (s_kpDel)               pad |= 0x20u; /* SELECT */
    if (s_ctrlHeld)            pad |= 0x80u; /* Any Control -> A */
    if (s_leftAlt || s_rightAlt) pad |= 0x40u; /* Any Alt -> B */

    static uint8_t  s_prevPad = 0;
    static uint32_t s_padHoldFrames = 0;
    static uint8_t  s_padHeldDir = 0;
    static bool     s_resetComboActive = false;
    uint8_t pad_pressed = (uint8_t)(pad & ~s_prevPad);

    /* Combo Start + A + B : Reset */
    if ((pad & 0xD0u) == 0xD0u) {
        if (!s_resetComboActive) {
            s_resetComboActive = true;
            clear_held_key();
            apple_reset_req(true);
        }
        s_prevPad = pad;
        return;
    } else {
        s_resetComboActive = false;
    }

    /* Bouton SELECT : bascule Disk UI (si Config Menu ferme) */
    if ((pad_pressed & 0x20u) && !apple_config_menu_is_open()) {
        clear_held_key();
        disk_ui_toggle();
    }

    if (disk_ui_is_visible()) {
        uint8_t dpad_held = pad & 0x0Fu;
        if (pad_pressed & 0x08u) { disk_ui_handle_key(0x0B); s_padHeldDir = 0x08u; s_padHoldFrames = 0; }
        if (pad_pressed & 0x04u) { disk_ui_handle_key(0x0A); s_padHeldDir = 0x04u; s_padHoldFrames = 0; }
        if (pad_pressed & 0x02u) { disk_ui_handle_key(0x08); s_padHeldDir = 0x02u; s_padHoldFrames = 0; }
        if (pad_pressed & 0x01u) { disk_ui_handle_key(0x15); s_padHeldDir = 0x01u; s_padHoldFrames = 0; }

        if (dpad_held && (dpad_held & s_padHeldDir)) {
            s_padHoldFrames++;
            if (s_padHoldFrames > KEY_REPEAT_INITIAL_DELAY) {
                uint32_t d = s_padHoldFrames - KEY_REPEAT_INITIAL_DELAY;
                if ((d % KEY_REPEAT_RATE) == 0) {
                    if (s_padHeldDir == 0x08u)      disk_ui_handle_key(0x0B);
                    else if (s_padHeldDir == 0x04u) disk_ui_handle_key(0x0A);
                    else if (s_padHeldDir == 0x02u) disk_ui_handle_key(0x08);
                    else if (s_padHeldDir == 0x01u) disk_ui_handle_key(0x15);
                }
            }
        } else {
            s_padHeldDir = 0;
            s_padHoldFrames = 0;
        }

        if (pad_pressed & 0x80u) disk_ui_handle_key(0x0D); /* A -> Enter */
        if (pad_pressed & 0x40u) disk_ui_handle_key(0x1B); /* B -> Esc */
        s_prevPad = pad;
        return;
    }

    s_prevPad = pad;

    if (!apple_config_menu_is_open()) {
        bool btn_a = ((pad & 0x80u) != 0) || s_leftAlt  || s_leftWin;
        bool btn_b = ((pad & 0x40u) != 0) || s_rightAlt || s_rightWin;
        bool btn_c = ((pad & 0x10u) != 0);

        #define PADDLE_CENTER 127
        #define PADDLE_SPEED  4
        static uint8_t joy_x = PADDLE_CENTER;
        static uint8_t joy_y = PADDLE_CENTER;

        if (pad & 0x02u) { /* Left */
            joy_x = (joy_x > PADDLE_SPEED) ? (uint8_t)(joy_x - PADDLE_SPEED) : 0;
        } else if (pad & 0x01u) { /* Right */
            joy_x = (joy_x < 255 - PADDLE_SPEED) ? (uint8_t)(joy_x + PADDLE_SPEED) : 255;
        } else {
            joy_x = PADDLE_CENTER;
        }

        if (pad & 0x08u) { /* Up */
            joy_y = (joy_y > PADDLE_SPEED) ? (uint8_t)(joy_y - PADDLE_SPEED) : 0;
        } else if (pad & 0x04u) { /* Down */
            joy_y = (joy_y < 255 - PADDLE_SPEED) ? (uint8_t)(joy_y + PADDLE_SPEED) : 255;
        } else {
            joy_y = PADDLE_CENTER;
        }

        apple_set_paddles(joy_x, joy_y, btn_a, btn_b, btn_c);
    }
}

} /* extern "C" */
