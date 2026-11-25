/*
 *  board_config.h - Hardware configuration for Apple IIe (MII)
 *                   on Waveshare RP2350-PiZero
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "config.h"
#include "hardware/vreg.h"

#ifndef CPU_CLOCK_MHZ
#define CPU_CLOCK_MHZ           252
#endif

// HDMI Display dimensions (640x480 @ 60Hz, 2x pixel doubling from 320x240)
#define HDMI_WIDTH              320
#define HDMI_HEIGHT             240
#define FB_WIDTH                320
#define FB_HEIGHT               240

// SD Card Configuration (SPI1 on GPIO 30/31/40/43)
#define SDCARD_SPI_BUS          SD_SPI_PORT
#define SDCARD_PIN_SPI0_CS      PIN_SD_CS
#define SDCARD_PIN_SPI0_SCK     PIN_SD_SCK
#define SDCARD_PIN_SPI0_MOSI    PIN_SD_MOSI
#define SDCARD_PIN_SPI0_MISO    PIN_SD_MISO

// PS/2 Keyboard Configuration (PIO2 SM0 on GPIO 16/17)
#define PS2_PIN_DATA            PIN_PS2_DATA
#define PS2_PIN_CLK             PIN_PS2_CLOCK
#ifndef ENABLE_PS2_KEYBOARD
#define ENABLE_PS2_KEYBOARD     1
#endif

// NES Gamepad Configuration (GPIO 5 CLK, GPIO 9 LATCH, GPIO 6 DATA)
#define NESPAD_GPIO_CLK         PIN_NES_CLOCK
#define NESPAD_GPIO_LATCH       PIN_NES_LATCH
#define NESPAD_GPIO_DATA        PIN_NES_DATA

// PSRAM Configuration (8MB APS6404L on GPIO 47)
#define PSRAM_CS_PIN            PIN_PSRAM_CS
#ifndef PSRAM_MAX_FREQ_MHZ
#define PSRAM_MAX_FREQ_MHZ      126
#endif
#define PSRAM_BASE_ADDR         PSRAM_XIP_CACHED_BASE
#define PSRAM_SIZE              (8 * 1024 * 1024)

// Also provide psram_init alias for frank-apple code if referenced
#define psram_init(pin)         board_psram_init(pin)

#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN    APPLE_PIN_LED
#endif

#endif /* BOARD_CONFIG_H */
