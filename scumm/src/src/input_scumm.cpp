#include "input_scumm.h"
#include "usb_kbd.h"
#include "ps2_kbd.h"
#include "nes_ctrl.h"
#include "scumm_osd.h"

static Ps2Decoder s_ps2Dec;
static bool s_shiftHeld = false;
static bool s_ctrlHeld  = false;
static bool s_altHeld   = false;

/* Conversion scancode PS/2 Set-2 -> USB HID usage (pour scumm_key_hid) */
static uint8_t ps2_set2_to_hid_usage(uint8_t code, bool ext)
{
    if (ext) {
        switch (code) {
            case 0x74: return 0x4F; /* Right */
            case 0x6B: return 0x50; /* Left */
            case 0x72: return 0x51; /* Down */
            case 0x75: return 0x52; /* Up */
            case 0x71: return 0x4C; /* Delete */
            case 0x5A: return 0x28; /* KP Enter -> Return */
            default:   return 0;
        }
    }
    switch (code) {
        case 0x1C: return 0x04; /* A */
        case 0x32: return 0x05; /* B */
        case 0x21: return 0x06; /* C */
        case 0x23: return 0x07; /* D */
        case 0x24: return 0x08; /* E */
        case 0x2B: return 0x09; /* F */
        case 0x34: return 0x0A; /* G */
        case 0x33: return 0x0B; /* H */
        case 0x43: return 0x0C; /* I */
        case 0x3B: return 0x0D; /* J */
        case 0x42: return 0x0E; /* K */
        case 0x4B: return 0x0F; /* L */
        case 0x3A: return 0x10; /* M */
        case 0x31: return 0x11; /* N */
        case 0x44: return 0x12; /* O */
        case 0x4D: return 0x13; /* P */
        case 0x15: return 0x14; /* Q */
        case 0x2D: return 0x15; /* R */
        case 0x1B: return 0x16; /* S */
        case 0x2C: return 0x17; /* T */
        case 0x3C: return 0x18; /* U */
        case 0x2A: return 0x19; /* V */
        case 0x1D: return 0x1A; /* W */
        case 0x22: return 0x1B; /* X */
        case 0x35: return 0x1C; /* Y */
        case 0x1A: return 0x1D; /* Z */
        case 0x16: return 0x1E; /* 1 */
        case 0x1E: return 0x1F; /* 2 */
        case 0x26: return 0x20; /* 3 */
        case 0x25: return 0x21; /* 4 */
        case 0x2E: return 0x22; /* 5 */
        case 0x36: return 0x23; /* 6 */
        case 0x3D: return 0x24; /* 7 */
        case 0x3E: return 0x25; /* 8 */
        case 0x46: return 0x26; /* 9 */
        case 0x45: return 0x27; /* 0 */
        case 0x5A: return 0x28; /* Return */
        case 0x76: return 0x29; /* Escape */
        case 0x66: return 0x2A; /* Backspace */
        case 0x0D: return 0x2B; /* Tab */
        case 0x29: return 0x2C; /* Space */
        case 0x4E: return 0x2D; /* - */
        case 0x55: return 0x2E; /* = */
        case 0x54: return 0x2F; /* [ */
        case 0x5B: return 0x30; /* ] */
        case 0x5D: return 0x31; /* \ */
        case 0x4C: return 0x33; /* ; */
        case 0x52: return 0x34; /* ' */
        case 0x0E: return 0x35; /* ` */
        case 0x41: return 0x36; /* , */
        case 0x49: return 0x37; /* . */
        case 0x4A: return 0x38; /* / */
        case 0x05: return 0x3A; /* F1 */
        case 0x06: return 0x3B; /* F2 */
        case 0x04: return 0x3C; /* F3 */
        case 0x0C: return 0x3D; /* F4 */
        case 0x03: return 0x3E; /* F5 */
        case 0x0B: return 0x3F; /* F6 */
        case 0x83: return 0x40; /* F7 */
        case 0x0A: return 0x41; /* F8 */
        case 0x01: return 0x42; /* F9 */
        case 0x09: return 0x43; /* F10 */
        case 0x78: return 0x44; /* F11 */
        case 0x07: return 0x45; /* F12 */
        default:   return 0;
    }
}

static void handle_key_event(const Ps2KeyEvent *ev, scumm_system *sys)
{
    uint8_t code = ev->code;
    bool pressed = ev->pressed;

    if (!ev->extended && (code == 0x12 || code == 0x59)) {
        s_shiftHeld = pressed;
        return;
    }
    if (code == 0x14) {
        s_ctrlHeld = pressed;
        return;
    }
    if (code == 0x11) {
        s_altHeld = pressed;
        return;
    }

    if (!ev->extended && code == 0x0C && pressed) { /* F4 : OSD Menu */
        if (scumm_osd_is_open()) scumm_osd_close();
        else scumm_osd_open();
        return;
    }

    if (!pressed) return;

    if (scumm_osd_is_open()) {
        if (ev->extended) {
            if (code == 0x75) scumm_osd_key(0x0B, sys);
            else if (code == 0x72) scumm_osd_key(0x0A, sys);
            else if (code == 0x6B) scumm_osd_key(0x08, sys);
            else if (code == 0x74) scumm_osd_key(0x15, sys);
        } else {
            if (code == 0x5A) scumm_osd_key(0x0D, sys);
            else if (code == 0x76) scumm_osd_key(0x1B, sys);
        }
        return;
    }

    if (!sys) return;
    uint8_t usage = ps2_set2_to_hid_usage(code, ev->extended);
    if (usage != 0) {
        uint8_t mod = 0;
        if (s_ctrlHeld)  mod |= 0x01u;
        if (s_shiftHeld) mod |= 0x02u;
        if (s_altHeld)   mod |= 0x04u;
        scumm_key_hid(sys, usage, mod);
    }
}

void input_scumm_init(void)
{
    s_shiftHeld = s_ctrlHeld = s_altHeld = false;
    ps2_decoder_reset(&s_ps2Dec);
    ps2kbd_begin();
    usb_kbd_init();
    nes_begin();
}

void input_scumm_poll(scumm_system *sys)
{
    /* 1. USB Dual-Host (Clavier + Souris + Hub + Manette) */
    usb_kbd_poll();
    Ps2KeyEvent ev;
    while (usb_kbd_pop_event(&ev)) {
        handle_key_event(&ev, sys);
    }

    /* 2. Clavier PS/2 */
    uint8_t raw = 0;
    while ((raw = ps2kbd_read()) != 0) {
        if (ps2_decoder_feed(&s_ps2Dec, raw, &ev)) {
            handle_key_event(&ev, sys);
        }
    }
    while (ps2_queue_pop(&raw)) {
        if (ps2_decoder_feed(&s_ps2Dec, raw, &ev)) {
            handle_key_event(&ev, sys);
        }
    }

    /* 3. Souris USB */
    if (sys && !scumm_osd_is_open()) {
        int32_t dx = 0, dy = 0;
        uint8_t btns = 0;
        usb_mouse_take(&dx, &dy, &btns);
        scumm_mouse_input(sys, dx, dy, btns);
    }

    /* 4. Manette NES (GPIO 5/6/9) + Manette USB */
    uint8_t nes_raw = 0xFF;
    static uint8_t s_lastNes = 0xFF;
    if (nes_poll_nonblocking(&nes_raw)) {
        s_lastNes = nes_raw;
    }
    uint8_t pad = (uint8_t)(~s_lastNes & 0xFFu);
    pad |= usb_gamepad_get_state();

    static bool s_prevMenuCombo = false;
    bool menu_combo = ((pad & 0x30u) == 0x30u); /* Select + Start */
    if (menu_combo && !s_prevMenuCombo) {
        if (scumm_osd_is_open()) scumm_osd_close();
        else scumm_osd_open();
    }
    s_prevMenuCombo = menu_combo;

    if (sys && !scumm_osd_is_open()) {
        uint32_t scumm_pad = 0;
        if (pad & 0x08u) scumm_pad |= SCUMM_PAD_UP;
        if (pad & 0x04u) scumm_pad |= SCUMM_PAD_DOWN;
        if (pad & 0x02u) scumm_pad |= SCUMM_PAD_LEFT;
        if (pad & 0x01u) scumm_pad |= SCUMM_PAD_RIGHT;
        if (pad & 0x80u) scumm_pad |= SCUMM_PAD_A;      /* A -> Clic gauche */
        if (pad & 0x40u) scumm_pad |= SCUMM_PAD_B;      /* B -> Clic droit  */
        if (pad & 0x10u) scumm_pad |= SCUMM_PAD_START;  /* Start -> Espace  */
        if (pad & 0x20u) scumm_pad |= SCUMM_PAD_SELECT; /* Select -> F5     */
        scumm_input_update(sys, scumm_pad);
    }
}
