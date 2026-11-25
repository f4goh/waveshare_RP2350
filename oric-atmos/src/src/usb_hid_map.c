/*
 * Table de correspondance USB HID usage -> scancode PS/2 Set-2
 * Prend en charge la disposition QWERTY (US) ou AZERTY (FR).
 * Separe en C99 pur pour compatibilite C++ (usb_kbd.cpp) et tests hote.
 */

#include "usb_kbd.h"
#include <string.h>

static bool s_azertyMode = false;

void usb_hid_set_azerty(bool azerty_fr)
{
    s_azertyMode = azerty_fr;
}

bool usb_hid_is_azerty(void)
{
    return s_azertyMode;
}

bool usb_hid_to_ps2_layout(uint8_t hid_usage, bool azerty_fr, uint8_t *ps2_code, bool *extended)
{
    typedef struct {
        uint8_t sc;
        uint8_t ext;
    } MapEntry;

    static const MapEntry table[104] = {
        [0x04] = {0x1Cu, 0}, [0x05] = {0x32u, 0}, [0x06] = {0x21u, 0}, [0x07] = {0x23u, 0}, /* A B C D */
        [0x08] = {0x24u, 0}, [0x09] = {0x2Bu, 0}, [0x0A] = {0x34u, 0}, [0x0B] = {0x33u, 0}, /* E F G H */
        [0x0C] = {0x43u, 0}, [0x0D] = {0x3Bu, 0}, [0x0E] = {0x42u, 0}, [0x0F] = {0x4Bu, 0}, /* I J K L */
        [0x10] = {0x3Au, 0}, [0x11] = {0x31u, 0}, [0x12] = {0x44u, 0}, [0x13] = {0x4Du, 0}, /* M N O P */
        [0x14] = {0x15u, 0}, [0x15] = {0x2Du, 0}, [0x16] = {0x1Bu, 0}, [0x17] = {0x2Cu, 0}, /* Q R S T */
        [0x18] = {0x3Cu, 0}, [0x19] = {0x2Au, 0}, [0x1A] = {0x1Du, 0}, [0x1B] = {0x22u, 0}, /* U V W X */
        [0x1C] = {0x35u, 0}, [0x1D] = {0x1Au, 0},                                           /* Y Z     */

        [0x1E] = {0x16u, 0}, [0x1F] = {0x1Eu, 0}, [0x20] = {0x26u, 0}, [0x21] = {0x25u, 0}, /* 1 2 3 4 */
        [0x22] = {0x2Eu, 0}, [0x23] = {0x36u, 0}, [0x24] = {0x3Du, 0}, [0x25] = {0x3Eu, 0}, /* 5 6 7 8 */
        [0x26] = {0x46u, 0}, [0x27] = {0x45u, 0},                                           /* 9 0     */

        [0x28] = {0x5Au, 0}, /* Enter */
        [0x29] = {0x76u, 0}, /* Escape */
        [0x2A] = {0x66u, 0}, /* Backspace */
        [0x2B] = {0x0Du, 0}, /* Tab */
        [0x2C] = {0x29u, 0}, /* Space */
        [0x2D] = {0x4Eu, 0}, /* - */
        [0x2E] = {0x55u, 0}, /* = */
        [0x2F] = {0x54u, 0}, /* [ */
        [0x30] = {0x5Bu, 0}, /* ] */
        [0x31] = {0x5Du, 0}, /* \ */
        [0x32] = {0x5Du, 0}, /* Non-US # / \ */
        [0x33] = {0x4Cu, 0}, /* ; */
        [0x34] = {0x52u, 0}, /* ' */
        [0x36] = {0x41u, 0}, /* , */
        [0x37] = {0x49u, 0}, /* . */
        [0x38] = {0x4Au, 0}, /* / */
        [0x39] = {0x58u, 0}, /* CapsLock */

        [0x3A] = {0x05u, 0}, /* F1 */
        [0x3B] = {0x06u, 0}, /* F2 */
        [0x3C] = {0x04u, 0}, /* F3 */
        [0x3D] = {0x0Cu, 0}, /* F4 (Configurateur) */
        [0x3E] = {0x03u, 0}, /* F5 */
        [0x3F] = {0x0Bu, 0}, /* F6 */
        [0x40] = {0x83u, 0}, /* F7 */
        [0x41] = {0x0Au, 0}, /* F8 */
        [0x42] = {0x01u, 0}, /* F9 */
        [0x43] = {0x09u, 0}, /* F10 */
        [0x44] = {0x78u, 0}, /* F11 */
        [0x45] = {0x07u, 0}, /* F12 */

        [0x49] = {0x70u, 1}, /* Insert */
        [0x4A] = {0x6Cu, 1}, /* Home */
        [0x4B] = {0x7Du, 1}, /* PageUp */
        [0x4C] = {0x71u, 1}, /* Delete */
        [0x4D] = {0x69u, 1}, /* End */
        [0x4E] = {0x7Au, 1}, /* PageDown */
        [0x4F] = {0x74u, 1}, /* Right */
        [0x50] = {0x6Bu, 1}, /* Left */
        [0x51] = {0x72u, 1}, /* Down */
        [0x52] = {0x75u, 1}, /* Up */

        [0x54] = {0x4Au, 1}, /* KP / */
        [0x56] = {0x7Bu, 0}, /* KP - */
        [0x58] = {0x5Au, 1}, /* KP Enter */
        [0x59] = {0x69u, 0}, /* KP 1 */
        [0x5A] = {0x72u, 0}, /* KP 2 */
        [0x5B] = {0x7Au, 0}, /* KP 3 */
        [0x5C] = {0x6Bu, 0}, /* KP 4 */
        [0x5D] = {0x73u, 0}, /* KP 5 */
        [0x5E] = {0x74u, 0}, /* KP 6 */
        [0x5F] = {0x6Cu, 0}, /* KP 7 */
        [0x60] = {0x75u, 0}, /* KP 8 */
        [0x61] = {0x7Du, 0}, /* KP 9 */
        [0x62] = {0x70u, 0}, /* KP 0 */
        [0x63] = {0x71u, 0}, /* KP . */
    };

    if (!ps2_code || !extended) {
        return false;
    }
    if (hid_usage >= sizeof(table) / sizeof(table[0]) || table[hid_usage].sc == 0u) {
        return false;
    }

    uint8_t sc  = table[hid_usage].sc;
    bool    ext = (table[hid_usage].ext != 0u);

    if (azerty_fr && !ext) {
        /* Echanges AZERTY (FR) <-> matrice physique QWERTY Oric */
        switch (hid_usage) {
            case 0x14u: sc = 0x1Cu; break; /* Touche 'A' AZERTY -> Oric A */
            case 0x04u: sc = 0x15u; break; /* Touche 'Q' AZERTY -> Oric Q */
            case 0x1Au: sc = 0x1Au; break; /* Touche 'Z' AZERTY -> Oric Z */
            case 0x1Du: sc = 0x1Du; break; /* Touche 'W' AZERTY -> Oric W */
            case 0x33u: sc = 0x3Au; break; /* Touche 'M' AZERTY -> Oric M */
            case 0x10u: sc = 0x41u; break; /* Touche ',' AZERTY -> Oric , */
            case 0x36u: sc = 0x4Cu; break; /* Touche ';' AZERTY -> Oric ; */
            case 0x37u: sc = 0x49u; break; /* Touche ':' AZERTY -> Oric . */
            case 0x38u: sc = 0x4Au; break; /* Touche '!' AZERTY -> Oric / */
            default: break;
        }
    }

    *ps2_code = sc;
    *extended = ext;
    return true;
}

bool usb_hid_to_ps2(uint8_t hid_usage, uint8_t *ps2_code, bool *extended)
{
    return usb_hid_to_ps2_layout(hid_usage, s_azertyMode, ps2_code, extended);
}

static bool key_in_6list(uint8_t key, const uint8_t list[6])
{
    for (int i = 0; i < 6; i++) {
        if (list[i] == key) {
            return true;
        }
    }
    return false;
}

void usb_hid_decode_kbd_report(const uint8_t *report,
                               uint16_t       len,
                               uint8_t        has_report_id,
                               uint8_t       *prev_mod_io,
                               uint8_t        prev_keys_io[6],
                               UsbKbdEmitFn   emit_cb,
                               void          *ctx)
{
    typedef struct {
        uint8_t mask;
        uint8_t sc;
        uint8_t ext;
    } ModEntry;

    static const ModEntry mods[5] = {
        {0x01u, 0x14u, 0u}, /* LCtrl */
        {0x02u, 0x12u, 0u}, /* LShift */
        {0x04u, 0x11u, 0u}, /* LAlt -> FCT Oric */
        {0x10u, 0x14u, 1u}, /* RCtrl */
        {0x20u, 0x59u, 0u}, /* RShift */
    };

    if (!report || !prev_mod_io || !prev_keys_io || !emit_cb) {
        return;
    }
    if (has_report_id) {
        if (len <= 1u) {
            return;
        }
        report++;
        len--;
    }

    uint8_t buf[8] = {0};
    uint16_t cp = (len < 8u) ? len : 8u;
    memcpy(buf, report, cp);

    /* Trame d'erreur rollover (0x01) ignoree pour ne pas perdre l'etat */
    if (buf[2] == 0x01u) {
        return;
    }

    uint8_t new_mod  = buf[0];
    uint8_t prev_mod = *prev_mod_io;
    uint8_t changed  = (uint8_t)(prev_mod ^ new_mod);
    if (changed != 0u) {
        for (size_t i = 0; i < 5u; i++) {
            if (changed & mods[i].mask) {
                emit_cb(mods[i].sc, mods[i].ext != 0u, (new_mod & mods[i].mask) != 0u, ctx);
            }
        }
    }

    const uint8_t *new_keys = &buf[2];
    for (int i = 0; i < 6; i++) {
        uint8_t prev = prev_keys_io[i];
        if (prev >= 0x04u && !key_in_6list(prev, new_keys)) {
            uint8_t sc = 0u;
            bool ext = false;
            if (usb_hid_to_ps2(prev, &sc, &ext)) {
                emit_cb(sc, ext, false, ctx);
            }
        }
    }
    for (int i = 0; i < 6; i++) {
        uint8_t cur = new_keys[i];
        if (cur >= 0x04u && !key_in_6list(cur, prev_keys_io)) {
            uint8_t sc = 0u;
            bool ext = false;
            if (usb_hid_to_ps2(cur, &sc, &ext)) {
                emit_cb(sc, ext, true, ctx);
            }
        }
    }

    *prev_mod_io = new_mod;
    memcpy(prev_keys_io, new_keys, 6u);
}
