/*
 * Gestion matrice clavier Oric Atmos (multi-touches USB HID + PS/2 + manette NES)
 */

#include "oric_keyboard.h"
#include "arduino_hal.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

OricKeyState kbdState;
OricKeyState nesState;

static const uint8_t s_defaultTableNes[NES_CSV_ROWS][NES_CSV_COLS] = {
    {   1, 7, 4 }, /* Droite */
    {   2, 5, 4 }, /* Gauche */
    {   4, 6, 4 }, /* Bas */
    {   8, 3, 4 }, /* Haut */
    {  16, 4, 4 }, /* Start -> Shift G */
    {  32, 4, 7 }, /* Select -> Shift D */
    {  64, 0, 4 }, /* B -> Space */
    { 128, 0, 4 }  /* A -> Space */
};

uint8_t tableNes[NES_CSV_ROWS][NES_CSV_COLS];
static uint8_t s_csvTable[NES_CSV_ROWS][NES_CSV_COLS];
static bool    s_hasCsv     = false;
static uint8_t s_nesPreset  = 0u;

static const uint8_t s_presetZqsd[NES_CSV_ROWS][NES_CSV_COLS] = {
    {   1, 7, 1 }, /* Droite -> D */
    {   2, 6, 1 }, /* Gauche -> Q */
    {   4, 6, 6 }, /* Bas    -> S */
    {   8, 5, 2 }, /* Haut   -> Z */
    {  16, 4, 4 }, /* Start  -> LShift */
    {  32, 4, 7 }, /* Select -> RShift */
    {  64, 5, 7 }, /* B      -> Return */
    { 128, 0, 4 }  /* A      -> Space */
};

static const uint8_t s_presetWasd[NES_CSV_ROWS][NES_CSV_COLS] = {
    {   1, 7, 1 }, /* Droite -> D */
    {   2, 5, 6 }, /* Gauche -> A */
    {   4, 6, 6 }, /* Bas    -> S */
    {   8, 7, 6 }, /* Haut   -> W */
    {  16, 4, 4 }, /* Start  -> LShift */
    {  32, 4, 7 }, /* Select -> RShift */
    {  64, 5, 7 }, /* B      -> Return */
    { 128, 0, 4 }  /* A      -> Space */
};

static const uint8_t s_presetIjkl[NES_CSV_ROWS][NES_CSV_COLS] = {
    {   1, 1, 7 }, /* Droite -> L */
    {   2, 0, 1 }, /* Gauche -> J */
    {   4, 0, 3 }, /* Bas    -> K */
    {   8, 1, 5 }, /* Haut   -> I */
    {  16, 4, 4 }, /* Start  -> LShift */
    {  32, 4, 7 }, /* Select -> RShift */
    {  64, 5, 7 }, /* B      -> Return */
    { 128, 0, 4 }  /* A      -> Space */
};

static void apply_active_nes_preset(void)
{
    switch (s_nesPreset) {
        case 1u:
            memcpy(tableNes, s_defaultTableNes, sizeof(tableNes));
            break;
        case 2u:
            memcpy(tableNes, s_presetZqsd, sizeof(tableNes));
            break;
        case 3u:
            memcpy(tableNes, s_presetWasd, sizeof(tableNes));
            break;
        case 4u:
            memcpy(tableNes, s_presetIjkl, sizeof(tableNes));
            break;
        default:
            memcpy(tableNes, s_hasCsv ? s_csvTable : s_defaultTableNes, sizeof(tableNes));
            break;
    }
}

static const uint8_t s_colMasks[8] = {
    0xFEu, 0xFDu, 0xFBu, 0xF7u, 0xEFu, 0xDFu, 0xBFu, 0x7Fu
};

typedef struct {
    uint8_t sc;
    uint8_t ext;
    uint8_t col;
    uint8_t row;
} KeyMapEntry;

static const KeyMapEntry s_keyMap[] = {
    /* ROW 0 : "7 N 5 V [RCtrl] 1 X 3" */
    {0x3Du, 0, 0xFEu, 0}, /* 7 */
    {0x31u, 0, 0xFDu, 0}, /* N */
    {0x2Eu, 0, 0xFBu, 0}, /* 5 */
    {0x2Au, 0, 0xF7u, 0}, /* V */
    {0x14u, 1, 0xEFu, 0}, /* Right Ctrl */
    {0x16u, 0, 0xDFu, 0}, /* 1 */
    {0x22u, 0, 0xBFu, 0}, /* X */
    {0x26u, 0, 0x7Fu, 0}, /* 3 */

    /* ROW 1 : "J T R F [none] ESC Q D" */
    {0x3Bu, 0, 0xFEu, 1}, /* J */
    {0x2Cu, 0, 0xFDu, 1}, /* T */
    {0x2Du, 0, 0xFBu, 1}, /* R */
    {0x2Bu, 0, 0xF7u, 1}, /* F */
    {0x76u, 0, 0xDFu, 1}, /* ESC */
    {0x15u, 0, 0xBFu, 1}, /* Q */
    {0x23u, 0, 0x7Fu, 1}, /* D */

    /* ROW 2 : "M 6 B 4 CTRL Z 2 C" */
    {0x3Au, 0, 0xFEu, 2}, /* M */
    {0x36u, 0, 0xFDu, 2}, /* 6 */
    {0x32u, 0, 0xFBu, 2}, /* B */
    {0x25u, 0, 0xF7u, 2}, /* 4 */
    {0x14u, 0, 0xEFu, 2}, /* Left Ctrl */
    {0x1Au, 0, 0xDFu, 2}, /* Z */
    {0x1Eu, 0, 0xBFu, 2}, /* 2 */
    {0x21u, 0, 0x7Fu, 2}, /* C */

    /* ROW 3 : "K 9 ; - [none] [none] \ '" */
    {0x42u, 0, 0xFEu, 3}, /* K */
    {0x46u, 0, 0xFDu, 3}, /* 9 */
    {0x4Cu, 0, 0xFBu, 3}, /* ; */
    {0x4Eu, 0, 0xF7u, 3}, /* - */
    {0x5Du, 0, 0xBFu, 3}, /* \ */
    {0x52u, 0, 0x7Fu, 3}, /* ' */

    /* ROW 4 : "SPACE , . UP LSHIFT LEFT DOWN RIGHT" */
    {0x29u, 0, 0xFEu, 4}, /* SPACE */
    {0x41u, 0, 0xFDu, 4}, /* , */
    {0x49u, 0, 0xFBu, 4}, /* . */
    {0x75u, 1, 0xF7u, 4}, /* UP */
    {0x12u, 0, 0xEFu, 4}, /* Left Shift */
    {0x6Bu, 1, 0xDFu, 4}, /* LEFT */
    {0x72u, 1, 0xBFu, 4}, /* DOWN */
    {0x74u, 1, 0x7Fu, 4}, /* RIGHT */

    /* ROW 5 : "U I O P FCT DEL ] [" */
    {0x3Cu, 0, 0xFEu, 5}, /* U */
    {0x43u, 0, 0xFDu, 5}, /* I */
    {0x44u, 0, 0xFBu, 5}, /* O */
    {0x4Du, 0, 0xF7u, 5}, /* P */
    {0x11u, 0, 0xEFu, 5}, /* FCT (Left Alt) */
    {0x66u, 0, 0xDFu, 5}, /* DEL (Backspace) */
    {0x71u, 1, 0xDFu, 5}, /* DEL (Suppr) */
    {0x5Bu, 0, 0xBFu, 5}, /* ] */
    {0x54u, 0, 0x7Fu, 5}, /* [ */

    /* ROW 6 : "Y H G E [none] A S W" */
    {0x35u, 0, 0xFEu, 6}, /* Y */
    {0x33u, 0, 0xFDu, 6}, /* H */
    {0x34u, 0, 0xFBu, 6}, /* G */
    {0x24u, 0, 0xF7u, 6}, /* E */
    {0x1Cu, 0, 0xDFu, 6}, /* A */
    {0x1Bu, 0, 0xBFu, 6}, /* S */
    {0x1Du, 0, 0x7Fu, 6}, /* W */

    /* ROW 7 : "8 L 0 / RSHIFT RETURN [CAPS] =" */
    {0x3Eu, 0, 0xFEu, 7}, /* 8 */
    {0x4Bu, 0, 0xFDu, 7}, /* L */
    {0x45u, 0, 0xFBu, 7}, /* 0 */
    {0x4Au, 0, 0xF7u, 7}, /* / */
    {0x59u, 0, 0xEFu, 7}, /* Right Shift */
    {0x5Au, 0, 0xDFu, 7}, /* RETURN */
    {0x55u, 0, 0x7Fu, 7}, /* = */

    /* Pave numerique */
    {0x70u, 0, 0xFBu, 7}, /* 0 */
    {0x69u, 0, 0xDFu, 0}, /* 1 */
    {0x72u, 0, 0xBFu, 2}, /* 2 */
    {0x7Au, 0, 0x7Fu, 0}, /* 3 */
    {0x6Bu, 0, 0xF7u, 2}, /* 4 */
    {0x73u, 0, 0xFBu, 0}, /* 5 */
    {0x74u, 0, 0xFDu, 2}, /* 6 */
    {0x6Cu, 0, 0xFEu, 0}, /* 7 */
    {0x75u, 0, 0xFEu, 7}, /* 8 */
    {0x7Du, 0, 0xFDu, 3}, /* 9 */
    {0x7Bu, 0, 0xF7u, 3}, /* - */
    {0x71u, 0, 0xFBu, 4}, /* . */
    {0x4Au, 1, 0xF7u, 7}, /* / */
    {0x5Au, 1, 0xDFu, 7}, /* Enter */
};

#define KEYMAP_COUNT (sizeof(s_keyMap) / sizeof(s_keyMap[0]))

void oric_keyboard_init(void)
{
    oric_keyboard_clear();
    s_hasCsv = false;
    memcpy(s_csvTable, s_defaultTableNes, sizeof(s_csvTable));
    apply_active_nes_preset();
}

void oric_keyboard_clear(void)
{
    kbdState.count = 0;
    nesState.count = 0;
}

static const KeyMapEntry *lookup_key(uint8_t sc, bool ext)
{
    uint8_t ext_u = ext ? 1u : 0u;
    for (size_t i = 0; i < KEYMAP_COUNT; i++) {
        if (s_keyMap[i].sc == sc && s_keyMap[i].ext == ext_u) {
            return &s_keyMap[i];
        }
    }
    return NULL;
}

void oric_keyboard_apply_event(const Ps2KeyEvent *ev)
{
    if (!ev || ev->code == 0u) {
        return;
    }

    if (!ev->pressed) {
        for (uint8_t i = 0; i < kbdState.count; i++) {
            if (kbdState.keys[i].sc == ev->code &&
                kbdState.keys[i].ext == ev->extended &&
                kbdState.keys[i].source == ev->source) {
                for (uint8_t j = i; j + 1u < kbdState.count; j++) {
                    kbdState.keys[j] = kbdState.keys[j + 1u];
                }
                kbdState.count--;
                break;
            }
        }
        return;
    }

    const KeyMapEntry *km = lookup_key(ev->code, ev->extended);
    if (!km) {
        return;
    }

    for (uint8_t i = 0; i < kbdState.count; i++) {
        if (kbdState.keys[i].sc == ev->code &&
            kbdState.keys[i].ext == ev->extended &&
            kbdState.keys[i].source == ev->source) {
            return;
        }
    }

    if (kbdState.count < ORIC_MAX_PRESSED_KEYS) {
        OricKeyEntry *e = &kbdState.keys[kbdState.count++];
        e->colonne = km->col;
        e->ligne   = km->row;
        e->sc      = ev->code;
        e->ext     = ev->extended;
        e->source  = ev->source;
    }
}

void __not_in_flash_func(oric_keyboard_nes)(uint8_t nes_bits)
{
    nesState.count = 0;

    for (uint8_t n = 0; n < NES_CSV_ROWS; n++) {
        if (tableNes[n][1] > 7u || tableNes[n][2] > 7u) {
            continue;
        }
        if (!(nes_bits & tableNes[n][0]) && nesState.count < ORIC_MAX_PRESSED_KEYS) {
            uint8_t col_idx = tableNes[n][1] & 7u;
            uint8_t row_idx = tableNes[n][2] & 7u;
            nesState.keys[nesState.count].colonne = s_colMasks[col_idx];
            nesState.keys[nesState.count].ligne   = row_idx;
            nesState.keys[nesState.count].sc      = 0;
            nesState.keys[nesState.count].ext     = false;
            nesState.keys[nesState.count].source  = 2u;
            nesState.count++;
        }
    }
}

typedef struct {
    uint8_t     col;
    uint8_t     row;
    const char *name;
} OricKeyName;

static const OricKeyName s_oricKeysFixed[] = {
    {255, 255, "AUCUN"},
    {  0,   4, "ESPACE"},
    {  5,   7, "RETURN"},
    {  4,   4, "L-SHIFT"},
    {  4,   7, "R-SHIFT"},
    {  4,   2, "CTRL"},
    {  5,   1, "ESC"},
    {  4,   5, "FCT"},
    {  5,   5, "DEL"},
    {  3,   4, "HAUT"},
    {  6,   4, "BAS"},
    {  5,   4, "GAUCHE"},
    {  7,   4, "DROITE"},
    {  5,   6, "A"}, {  2,   2, "B"}, {  7,   2, "C"}, {  7,   1, "D"},
    {  3,   6, "E"}, {  3,   1, "F"}, {  2,   6, "G"}, {  1,   6, "H"},
    {  1,   5, "I"}, {  0,   1, "J"}, {  0,   3, "K"}, {  1,   7, "L"},
    {  0,   2, "M"}, {  1,   0, "N"}, {  2,   5, "O"}, {  3,   5, "P"},
    {  6,   1, "Q"}, {  2,   1, "R"}, {  6,   6, "S"}, {  1,   1, "T"},
    {  0,   5, "U"}, {  3,   0, "V"}, {  7,   6, "W"}, {  6,   0, "X"},
    {  0,   6, "Y"}, {  5,   2, "Z"},
    {  2,   7, "0"}, {  5,   0, "1"}, {  6,   2, "2"}, {  7,   0, "3"},
    {  3,   2, "4"}, {  2,   0, "5"}, {  1,   2, "6"}, {  0,   0, "7"},
    {  0,   7, "8"}, {  1,   3, "9"},
    {  1,   4, ","}, {  2,   4, "."}, {  2,   3, ";"}, {  3,   7, "/"},
    {  3,   3, "-"}, {  7,   7, "="}, {  7,   5, "["}, {  6,   5, "]"},
    {  6,   3, "\\"}, { 7,   3, "'"}
};

#define ORIC_KEY_LIST_COUNT (sizeof(s_oricKeysFixed) / sizeof(s_oricKeysFixed[0]))

const char *oric_keyboard_key_name(uint8_t col_idx, uint8_t row_idx)
{
    if (col_idx > 7u || row_idx > 7u) {
        return "AUCUN";
    }
    for (size_t i = 0; i < ORIC_KEY_LIST_COUNT; i++) {
        if (s_oricKeysFixed[i].col == col_idx && s_oricKeysFixed[i].row == row_idx) {
            return s_oricKeysFixed[i].name;
        }
    }
    return "TOUCHE";
}

bool oric_keyboard_scancode_to_matrix(uint8_t sc, bool ext, uint8_t *col_idx, uint8_t *row_idx)
{
    const KeyMapEntry *km = lookup_key(sc, ext);
    if (!km) {
        return false;
    }
    for (uint8_t c = 0; c < 8u; c++) {
        if (s_colMasks[c] == km->col) {
            *col_idx = c;
            *row_idx = km->row;
            return true;
        }
    }
    return false;
}

void oric_keyboard_set_nes_key(uint8_t nes_idx, uint8_t col_idx, uint8_t row_idx)
{
    if (nes_idx >= NES_CSV_ROWS) {
        return;
    }
    s_nesPreset = 0u;
    s_hasCsv = true;
    s_csvTable[nes_idx][1] = col_idx;
    s_csvTable[nes_idx][2] = row_idx;
    tableNes[nes_idx][1]   = col_idx;
    tableNes[nes_idx][2]   = row_idx;
}

void oric_keyboard_get_nes_key(uint8_t nes_idx, uint8_t *col_idx, uint8_t *row_idx)
{
    if (nes_idx >= NES_CSV_ROWS) {
        *col_idx = 255u;
        *row_idx = 255u;
        return;
    }
    *col_idx = tableNes[nes_idx][1];
    *row_idx = tableNes[nes_idx][2];
}

void oric_keyboard_cycle_nes_key(uint8_t nes_idx, int delta)
{
    if (nes_idx >= NES_CSV_ROWS) {
        return;
    }
    uint8_t cur_c = tableNes[nes_idx][1];
    uint8_t cur_r = tableNes[nes_idx][2];
    int idx = 0;
    for (size_t i = 0; i < ORIC_KEY_LIST_COUNT; i++) {
        if (s_oricKeysFixed[i].col == cur_c && s_oricKeysFixed[i].row == cur_r) {
            idx = (int)i;
            break;
        }
    }
    int cnt = (int)ORIC_KEY_LIST_COUNT;
    idx = (idx + cnt + delta) % cnt;
    memcpy(s_csvTable, tableNes, sizeof(s_csvTable));
    oric_keyboard_set_nes_key(nes_idx, s_oricKeysFixed[idx].col, s_oricKeysFixed[idx].row);
}

void oric_keyboard_set_nes_preset(uint8_t preset)
{
    if (preset > 4u) {
        preset = 0u;
    }
    s_nesPreset = preset;
    apply_active_nes_preset();
}

uint8_t oric_keyboard_get_nes_preset(void)
{
    return s_nesPreset;
}

void oric_keyboard_load_nes_csv(const char *game_base_name)
{
    (void)game_base_name;
    /* Desormais gere directement depuis le menu Manette (5:PAD) sans fichier CSV */
}
