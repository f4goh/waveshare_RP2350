/*
 * Abstraction entree clavier unifiee : USB HID (PIO-USB) + PS/2 (PIO2)
 */

#include "input_kbd.h"
#include "usb_kbd.h"

static Ps2Decoder s_ps2Dec;

void input_kbd_init(void)
{
    ps2_decoder_reset(&s_ps2Dec);
    ps2_queue_clear();
    usb_kbd_init();
}

void input_kbd_clear(void)
{
    ps2_decoder_reset(&s_ps2Dec);
    ps2_queue_clear();
    usb_kbd_queue_clear();
}

bool input_kbd_poll(Ps2KeyEvent *ev)
{
    usb_kbd_poll();

    if (usb_kbd_pop_event(ev)) {
        return true;
    }

    uint8_t raw = 0;
    while (ps2_queue_pop(&raw)) {
        if (ps2_decoder_feed(&s_ps2Dec, raw, ev)) {
            return true;
        }
    }

    return false;
}
