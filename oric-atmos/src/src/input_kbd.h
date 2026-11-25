/*
 * Abstraction entree clavier unifiee : USB HID (PIO-USB) + PS/2 (PIO2)
 */

#ifndef INPUT_KBD_H
#define INPUT_KBD_H

#include <stdbool.h>
#include "ps2_kbd.h"

#ifdef __cplusplus
extern "C" {
#endif

void input_kbd_init(void);
void input_kbd_clear(void);
bool input_kbd_poll(Ps2KeyEvent *ev);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_KBD_H */
