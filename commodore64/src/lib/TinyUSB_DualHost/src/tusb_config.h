#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_RP2040
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

/* Dual USB Host: rhport 0 = Native RP2350 USBCTRL, rhport 1 = PIO-USB (GPIO 28/29) */
#define CFG_TUD_ENABLED       0
#define CFG_TUH_ENABLED       1
#define CFG_TUH_RPI_PIO_USB   1
#define BOARD_TUH_RHPORT      0
#define CFG_TUH_MAX_SPEED     OPT_MODE_DEFAULT_SPEED

#define CFG_TUH_TASK_QUEUE_SZ       64
#define CFG_TUH_ENUMERATION_BUFSIZE 1024

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

/* Compter les asserts TinyUSB recuperables sans figer sur bkpt */
extern void oric_tusb_assert_hook(void);
#define CFG_TUSB_DEBUG_BREAKPOINT oric_tusb_assert_hook

#define CFG_TUH_HUB         1
#define CFG_TUH_HID         8
#define CFG_TUH_MSC         0
#define CFG_TUH_CDC         0
#define CFG_TUH_MIDI        0
#define CFG_TUH_VENDOR      0
#define CFG_TUH_XINPUT      0
#define CFG_TUH_DEVICE_MAX  8

#define CFG_TUH_HID_EPIN_BUFSIZE  64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
