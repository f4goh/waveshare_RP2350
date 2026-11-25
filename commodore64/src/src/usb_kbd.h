/*
 * Pilote clavier USB HID Dual-Host :
 *   - rhport 0 : USB natif RP2350 (Connecteur Type-C "USB")
 *   - rhport 1 : Pico-PIO-USB sur PIO1 (Connecteur Type-C "PIO-USB", GPIO 28/29)
 */

#ifndef USB_KBD_H
#define USB_KBD_H

#include <stdbool.h>
#include <stdint.h>
#include "ps2_kbd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_KBD_MAX_SLOTS 8U

typedef struct {
    bool     mounted;
    uint8_t  rhport;       /* 0 = Native USB, 1 = PIO-USB */
    uint8_t  dev_addr;
    uint8_t  instance;
    uint8_t  itf_protocol; /* 0 = Report/Generic, 1 = Keyboard */
    uint16_t vid;
    uint16_t pid;
    uint32_t report_total;
    bool     is_keyboard;
} UsbKbdDevInfo;

typedef struct {
    bool          rh0_active;
    bool          rh1_active;
    uint32_t      tx_timeouts;
    uint32_t      tusb_asserts;
    uint32_t      rearm_count;
    uint32_t      desync_count;
    uint32_t      enum_fails;
    uint8_t       enum_state;
    uint8_t       mounted_count;
    UsbKbdDevInfo devs[USB_KBD_MAX_SLOTS];
} UsbKbdStats;

void usb_kbd_init(void);
void usb_kbd_poll(void);
bool usb_kbd_pop_event(Ps2KeyEvent *ev);
void usb_kbd_queue_clear(void);
void usb_kbd_get_stats(UsbKbdStats *out);

void usb_hid_set_azerty(bool azerty_fr);
bool usb_hid_is_azerty(void);
bool usb_hid_to_ps2_layout(uint8_t hid_usage, bool azerty_fr, uint8_t *ps2_code, bool *extended);
bool usb_hid_to_ps2(uint8_t hid_usage, uint8_t *ps2_code, bool *extended);

/* Decode un rapport HID Clavier (Boot 8 octets ou Report avec offset ID) */
typedef void (*UsbKbdEmitFn)(uint8_t ps2_scancode, bool extended, bool pressed, void *ctx);
void usb_hid_decode_kbd_report(const uint8_t *report,
                               uint16_t       len,
                               uint8_t        has_report_id,
                               uint8_t       *prev_mod_io,
                               uint8_t        prev_keys_io[6],
                               UsbKbdEmitFn   emit_cb,
                               void          *ctx);

#ifdef __cplusplus
}
#endif

#endif /* USB_KBD_H */
