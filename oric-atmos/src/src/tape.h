/*
 * Gestionnaire cassette TAP en streaming SD + menu selection Oric
 */

#ifndef ORIC_TAPE_H
#define ORIC_TAPE_H

#include <stdbool.h>
#include <stdint.h>

void    tape_init(void);
void    tape_reset(void);

/* Appele par busRead lors des hooks ROM (fetch instruction uniquement) */
void    tape_on_e4ac(uint8_t *oric_ram);
void    tape_on_e735(uint8_t *oric_ram);
uint8_t tape_read_byte(void);
void    tape_csave(const uint8_t *oric_ram);

bool        tape_is_active(void);
uint32_t    tape_bytes_remaining(void);
bool        tape_insert_file(const char *base_name);
const char *tape_current_name(void);
char      (*tape_shared_file_list(void))[48];

#endif /* ORIC_TAPE_H */
