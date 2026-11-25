/*
 * Gestion matrice clavier Oric Atmos (multi-touches USB + PS/2)
 */

#ifndef ORIC_KEYBOARD_H
#define ORIC_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "ps2_kbd.h"

#define ORIC_MAX_PRESSED_KEYS 8U

typedef struct {
    uint8_t colonne; /* Masque actif bas (ex: 0xFE = colonne 0) */
    uint8_t ligne;   /* Index ligne 0..7 */
    uint8_t sc;      /* Scancode PS/2 pour suivi presse/relache */
    bool    ext;     /* Flag E0 */
    uint8_t source;  /* 0 = PS/2, 1 = USB */
} OricKeyEntry;

typedef struct {
    uint8_t      count;
    OricKeyEntry keys[ORIC_MAX_PRESSED_KEYS];
} OricKeyState;

extern OricKeyState kbdState;
extern OricKeyState nesState;
extern uint8_t      tableNes[NES_CSV_ROWS][NES_CSV_COLS];

void oric_keyboard_init(void);
void oric_keyboard_clear(void);
void oric_keyboard_apply_event(const Ps2KeyEvent *ev);
void oric_keyboard_nes(uint8_t nes_bits);
void oric_keyboard_load_nes_csv(const char *game_base_name);
void oric_keyboard_set_nes_preset(uint8_t preset);
uint8_t oric_keyboard_get_nes_preset(void);
void oric_keyboard_set_nes_key(uint8_t nes_idx, uint8_t col_idx, uint8_t row_idx);
void oric_keyboard_get_nes_key(uint8_t nes_idx, uint8_t *col_idx, uint8_t *row_idx);
void oric_keyboard_cycle_nes_key(uint8_t nes_idx, int delta);
const char *oric_keyboard_key_name(uint8_t col_idx, uint8_t row_idx);
bool oric_keyboard_scancode_to_matrix(uint8_t sc, bool ext, uint8_t *col_idx, uint8_t *row_idx);

#endif /* ORIC_KEYBOARD_H */
