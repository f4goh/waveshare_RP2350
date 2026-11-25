/*
 * Pilote clavier USB HID Dual-Host (Native USBCTRL rhport 0 + PIO-USB rhport 1)
 * avec support Boot + Report Protocol (composite / gaming / dongles),
 * re-armement automatique watchdog et diagnostics pour le configurateur.
 */

#include <Arduino.h>
#include "tusb.h"
#include "pio_usb.h"
#include "usb_hcd_router.h"
#include "hid_rip.h"

#include "usb_kbd.h"
#include "config.h"
#include "hardware/dma.h"
#include "pico/time.h"

#include <string.h>

#define USB_EVT_QUEUE_SIZE 64U
#define USB_EVT_QUEUE_MASK (USB_EVT_QUEUE_SIZE - 1U)
#define MAX_HID_REPORT_INFOS 8U

typedef struct {
    bool                 active;
    uint8_t              dev_addr;
    uint8_t              instance;
    uint8_t              rhport;
    uint8_t              itf_protocol;
    bool                 is_keyboard;
    bool                 has_report_id;
    uint8_t              kbd_report_id;
    uint16_t             vid;
    uint16_t             pid;
    uint32_t             report_total;
    uint32_t             last_report_ms;
    uint32_t             stale_since_ms;
    uint8_t              prev_mod;
    uint8_t              prev_keys[6];
    uint8_t              report_count;
    tuh_hid_report_info_t report_info[MAX_HID_REPORT_INFOS];
} UsbKbdSlot;

static UsbKbdSlot  s_slots[USB_KBD_MAX_SLOTS];
static Ps2KeyEvent s_usbQueue[USB_EVT_QUEUE_SIZE];
static uint8_t     s_qHead = 0;
static uint8_t     s_qTail = 0;
static bool        s_rh0Ready = false;
static bool        s_rh1Ready = false;
static uint32_t    s_rearmCount = 0;
static uint32_t    s_desyncCount = 0;
static uint32_t    s_lastRearmTickMs = 0;
static uint32_t    s_lastAssertSeen = 0;

extern "C" {
volatile uint32_t g_tusb_assert_count = 0;
volatile uint32_t g_tusb_assert_last_pc = 0;
void __attribute__((noinline)) oric_tusb_assert_hook(void)
{
    g_tusb_assert_count++;
    g_tusb_assert_last_pc = (uint32_t)(uintptr_t)__builtin_return_address(0);
}
}

static inline uint32_t usb_now_ms(void)
{
    return (uint32_t)(time_us_64() / 1000u);
}

static void push_usb_ev(uint8_t code, bool ext, bool pressed, void *ctx)
{
    (void)ctx;
    if (code == 0u) {
        return;
    }
    uint8_t next = (uint8_t)((s_qHead + 1u) & USB_EVT_QUEUE_MASK);
    if (next != s_qTail) {
        s_usbQueue[s_qHead].code     = code;
        s_usbQueue[s_qHead].extended = ext;
        s_usbQueue[s_qHead].pressed  = pressed;
        s_usbQueue[s_qHead].source   = 1u;
        s_qHead = next;
    }
}

static UsbKbdSlot *find_slot(uint8_t dev_addr, uint8_t instance)
{
    for (size_t i = 0; i < USB_KBD_MAX_SLOTS; i++) {
        if (s_slots[i].active && s_slots[i].dev_addr == dev_addr && s_slots[i].instance == instance) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static UsbKbdSlot *alloc_slot(uint8_t dev_addr, uint8_t instance)
{
    UsbKbdSlot *existing = find_slot(dev_addr, instance);
    if (existing) {
        return existing;
    }
    for (size_t i = 0; i < USB_KBD_MAX_SLOTS; i++) {
        if (!s_slots[i].active) {
            memset(&s_slots[i], 0, sizeof(s_slots[i]));
            s_slots[i].active   = true;
            s_slots[i].dev_addr = dev_addr;
            s_slots[i].instance = instance;
            return &s_slots[i];
        }
    }
    return NULL;
}

static void release_slot_keys(UsbKbdSlot *slot)
{
    if (!slot || !slot->is_keyboard) {
        return;
    }
    static const uint8_t empty[8] = {0};
    usb_hid_decode_kbd_report(empty, 8u, 0u, &slot->prev_mod, slot->prev_keys, push_usb_ev, NULL);
}

static void usb_hid_rearm_tick(void)
{
    const uint32_t now = usb_now_ms();
    if ((uint32_t)(now - s_lastRearmTickMs) < 50u) {
        return;
    }
    s_lastRearmTickMs = now;

    for (size_t i = 0; i < USB_KBD_MAX_SLOTS; i++) {
        UsbKbdSlot *s = &s_slots[i];
        if (!s->active || !s->is_keyboard) {
            continue;
        }
        if (s->report_total == 0u || s->last_report_ms != s->stale_since_ms) {
            s->stale_since_ms = s->last_report_ms;
        }

        if (tuh_hid_receive_ready(s->dev_addr, s->instance)) {
            if (tuh_hid_receive_report(s->dev_addr, s->instance)) {
                s_rearmCount++;
            }
            continue;
        }

        uint32_t silent = (uint32_t)(now - s->stale_since_ms);
        bool evidence = false;
        if (g_tusb_assert_count != s_lastAssertSeen && silent > 1000u) {
            evidence = true;
        } else if (s->rhport == ORIC_RHPORT_PIO && silent > 5000u) {
            uint8_t ep_in = tuh_hid_ep_in(s->dev_addr, s->instance);
            uint32_t epdbg = ep_in ? pio_usb_host_ep_debug(s->dev_addr, ep_in) : ~0u;
            if (epdbg != ~0u && (epdbg & 1u) == 0u) {
                evidence = true;
            }
        }

        if (evidence) {
            s_lastAssertSeen = g_tusb_assert_count;
            tuh_hid_receive_abort(s->dev_addr, s->instance);
            if (tuh_hid_receive_report(s->dev_addr, s->instance)) {
                s_desyncCount++;
                s->stale_since_ms = now;
            }
        }
    }
}

void usb_kbd_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    s_qHead = s_qTail = 0;
    s_rearmCount = 0;
    s_desyncCount = 0;
    s_lastRearmTickMs = 0;
    s_lastAssertSeen = g_tusb_assert_count;

    /* 1. Port USB natif RP2350 (Connecteur Type-C "USB", rhport 0) */
    const tusb_rhport_init_t rh0_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,
    };
    s_rh0Ready = tusb_init(ORIC_RHPORT_NATIVE, &rh0_init);

    /* 2. Port PIO-USB (Connecteur Type-C "PIO-USB", GPIO 28/29, PIO1, rhport 1)
     * Allocation DMA par le haut pour eviter toute collision avec HDMI/SPI/I2S. */
    int free_tx_ch = -1;
    for (int ch = (int)NUM_DMA_CHANNELS - 1; ch >= 0; --ch) {
        if (!dma_channel_is_claimed((uint)ch)) {
            free_tx_ch = ch;
            break;
        }
    }

    if (free_tx_ch >= 0) {
        pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
        pio_cfg.pin_dp     = PIN_USB_HOST_DP;
        pio_cfg.pio_tx_num = 1;
        pio_cfg.sm_tx      = 0;
        pio_cfg.tx_ch      = (uint8_t)free_tx_ch;
        pio_cfg.pio_rx_num = 1;
        pio_cfg.sm_rx      = 1;
        pio_cfg.sm_eop     = 2;
        pio_cfg.pinout     = PIO_USB_PINOUT_DPDM;

        tuh_configure(ORIC_RHPORT_PIO, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
        const tusb_rhport_init_t rh1_init = {
            .role = TUSB_ROLE_HOST,
            .speed = TUSB_SPEED_FULL,
        };
        s_rh1Ready = tusb_init(ORIC_RHPORT_PIO, &rh1_init);
        if (s_rh1Ready) {
            pio_usb_host_reclock();
        }
    } else {
        s_rh1Ready = false;
    }
}

void usb_kbd_poll(void)
{
    if (s_rh0Ready || s_rh1Ready) {
        tuh_task_ext(0u, false);
        usb_hid_rearm_tick();
    }
}

bool usb_kbd_pop_event(Ps2KeyEvent *ev)
{
    if (s_qTail == s_qHead) {
        return false;
    }
    *ev = s_usbQueue[s_qTail];
    s_qTail = (uint8_t)((s_qTail + 1u) & USB_EVT_QUEUE_MASK);
    return true;
}

void usb_kbd_queue_clear(void)
{
    s_qTail = s_qHead;
}

void usb_kbd_get_stats(UsbKbdStats *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->rh0_active   = s_rh0Ready;
    out->rh1_active   = s_rh1Ready;
    out->tx_timeouts  = pio_usb_tx_timeouts;
    out->tusb_asserts = g_tusb_assert_count;
    out->rearm_count  = s_rearmCount;
    out->desync_count = s_desyncCount;

    tuh_enum_diag_t ed;
    tuh_enum_diag_get(&ed);
    out->enum_fails = ed.fails;
    out->enum_state = ed.state;

    for (size_t i = 0; i < USB_KBD_MAX_SLOTS; i++) {
        out->devs[i].mounted      = s_slots[i].active;
        out->devs[i].rhport       = s_slots[i].rhport;
        out->devs[i].dev_addr     = s_slots[i].dev_addr;
        out->devs[i].instance     = s_slots[i].instance;
        out->devs[i].itf_protocol = s_slots[i].itf_protocol;
        out->devs[i].vid          = s_slots[i].vid;
        out->devs[i].pid          = s_slots[i].pid;
        out->devs[i].report_total = s_slots[i].report_total;
        out->devs[i].is_keyboard  = s_slots[i].is_keyboard;
        if (s_slots[i].active) {
            out->mounted_count++;
        }
    }
}

extern "C" {

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    tuh_bus_info_t bus_info;
    memset(&bus_info, 0, sizeof(bus_info));
    tuh_bus_info_get(dev_addr, &bus_info);

    UsbKbdSlot *slot = alloc_slot(dev_addr, instance);
    if (!slot) {
        return;
    }

    slot->rhport         = bus_info.rhport;
    slot->itf_protocol   = itf_protocol;
    slot->vid            = vid;
    slot->pid            = pid;
    slot->last_report_ms = usb_now_ms();
    slot->stale_since_ms = slot->last_report_ms;

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        slot->is_keyboard = true;
    } else if (itf_protocol == HID_ITF_PROTOCOL_NONE && desc_report && desc_len) {
        slot->report_count = tuh_hid_parse_report_descriptor(
            slot->report_info, MAX_HID_REPORT_INFOS, desc_report, desc_len);

        if (slot->report_count == 0u) {
            tuh_hid_report_info_plus_t rip[MAX_HID_REPORT_INFOS];
            uint8_t n = tuh_hid_parse_report_descriptor_plus(
                rip, MAX_HID_REPORT_INFOS, desc_report, desc_len);
            if (n > MAX_HID_REPORT_INFOS) {
                n = MAX_HID_REPORT_INFOS;
            }
            for (uint8_t i = 0; i < n; i++) {
                slot->report_info[i].report_id  = rip[i].report_id;
                slot->report_info[i].usage      = (uint8_t)rip[i].usage;
                slot->report_info[i].usage_page = rip[i].usage_page;
            }
            slot->report_count = n;
        }

        for (uint8_t i = 0; i < slot->report_count; i++) {
            if (slot->report_info[i].usage_page == HID_USAGE_PAGE_DESKTOP &&
                slot->report_info[i].usage == HID_USAGE_DESKTOP_KEYBOARD) {
                slot->is_keyboard   = true;
                slot->kbd_report_id = slot->report_info[i].report_id;
                slot->has_report_id = !(slot->report_count == 1u && slot->report_info[0].report_id == 0u);
                break;
            }
        }
    }

    /* Toujours armer la reception immediatement, meme sur protocole non-boot ou composite */
    if (slot->is_keyboard) {
        tuh_hid_receive_report(dev_addr, instance);
    }
}

void tuh_hid_set_protocol_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t protocol)
{
    (void)protocol;
    UsbKbdSlot *slot = find_slot(dev_addr, instance);
    if (slot && slot->is_keyboard && tuh_hid_receive_ready(dev_addr, instance)) {
        tuh_hid_receive_report(dev_addr, instance);
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    UsbKbdSlot *slot = find_slot(dev_addr, instance);
    if (!slot) {
        return;
    }
    release_slot_keys(slot);
    memset(slot, 0, sizeof(*slot));
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
    UsbKbdSlot *slot = find_slot(dev_addr, instance);
    if (!slot) {
        return;
    }

    if (report && len > 0u) {
        slot->report_total++;
        slot->last_report_ms = usb_now_ms();
        slot->stale_since_ms = slot->last_report_ms;

        if (slot->itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
            usb_hid_decode_kbd_report(report, len, 0u,
                                      &slot->prev_mod, slot->prev_keys,
                                      push_usb_ev, NULL);
        } else if (slot->is_keyboard) {
            if (slot->has_report_id) {
                if (report[0] == slot->kbd_report_id && len > 1u) {
                    usb_hid_decode_kbd_report(report + 1, (uint16_t)(len - 1u), 0u,
                                              &slot->prev_mod, slot->prev_keys,
                                              push_usb_ev, NULL);
                }
            } else {
                usb_hid_decode_kbd_report(report, len, 0u,
                                          &slot->prev_mod, slot->prev_keys,
                                          push_usb_ev, NULL);
            }
        }
    }

    if (slot->is_keyboard) {
        tuh_hid_receive_report(dev_addr, instance);
    }
}

} /* extern "C" */
