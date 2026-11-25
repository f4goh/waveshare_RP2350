/*
 * Pilote clavier PS/2 via PIO2 SM0 + decodeur Set-2
 */

#ifndef PS2_KBD_H
#define PS2_KBD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t code;      /* Scancode PS/2 Set-2 */
    bool    extended;  /* Prefixe 0xE0 recu */
    bool    pressed;   /* true = appui, false = relachement (0xF0) */
    uint8_t source;    /* 0 = PS/2, 1 = USB HID */
} Ps2KeyEvent;

typedef struct {
    bool extended;
    bool release;
} Ps2Decoder;

bool    ps2kbd_begin(void);
uint8_t ps2kbd_read(void);
void    ps2kbd_write(uint8_t value);

/* File SPSC Core 1 -> Core 0 */
void ps2_queue_push(uint8_t raw_byte);
bool ps2_queue_pop(uint8_t *raw_byte);
void ps2_queue_clear(void);

/* Decode un octet PS/2 brut en evenement touche complet */
void ps2_decoder_reset(Ps2Decoder *dec);
bool ps2_decoder_feed(Ps2Decoder *dec, uint8_t raw_byte, Ps2KeyEvent *ev);

/* Disposition AZERTY FR / QWERTY US pour clavier PS/2 physique */
void ps2_set_azerty(bool azerty_fr);
bool ps2_is_azerty(void);

/* Convertit un scancode Set-2 non-etendu en ASCII majuscule (pour le moniteur) */
char ps2_scancode_to_ascii(uint8_t code);

#ifdef __cplusplus
}
#endif

#endif /* PS2_KBD_H */
