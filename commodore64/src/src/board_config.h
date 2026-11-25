/*
 *  board_config.h - Hardware configuration for Commodore 64 (Frodo4)
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
#define HDMI_WIDTH              640
#define HDMI_HEIGHT             480
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
#define ENABLE_PS2_KEYBOARD     1

// NES Gamepad Configuration (GPIO 5 CLK, GPIO 9 LATCH, GPIO 6 DATA)
#define NESPAD_GPIO_CLK         PIN_NES_CLOCK
#define NESPAD_GPIO_LATCH       PIN_NES_LATCH
#define NESPAD_GPIO_DATA        PIN_NES_DATA

// Audio Configuration (48 kHz HDMI Data Island / I2S PCM5102 / 96 kHz PWM)
#define SID_SAMPLE_RATE         SAMPLE_FREQUENCY
#define SID_BUFFER_SAMPLES      512

// PSRAM Configuration (8MB APS6404L on GPIO 47)
#define PSRAM_CS_PIN            PIN_PSRAM_CS
#ifndef PSRAM_MAX_FREQ_MHZ
#define PSRAM_MAX_FREQ_MHZ      133
#endif
#define PSRAM_BASE_ADDR         PSRAM_XIP_CACHED_BASE
#define PSRAM_SIZE              (8 * 1024 * 1024)

// C64 Emulation Configuration (PAL 6569 / 6581)
#define C64_RAM_SIZE            (64 * 1024)
#define C64_COLOR_RAM_SIZE      (1024)
#define DRIVE_1541_RAM_SIZE     (2 * 1024)

#define C64_TOTAL_RASTERS       312
#define C64_CYCLES_PER_LINE     63
#define C64_SCREEN_FREQ         50
#define SID_CLOCK_PAL           985248
#define SID_CLOCK_NTSC          1022727
#define SID_CLOCK_FREQ          SID_CLOCK_PAL

#define BASIC_ROM_SIZE          (8 * 1024)
#define KERNAL_ROM_SIZE         (8 * 1024)
#define CHAR_ROM_SIZE           (4 * 1024)
#define DRIVE_ROM_SIZE          (16 * 1024)

#define C64_BASIC_ROM_SIZE      BASIC_ROM_SIZE
#define C64_KERNAL_ROM_SIZE     KERNAL_ROM_SIZE
#define C64_CHAR_ROM_SIZE       CHAR_ROM_SIZE
#define DRIVE_1541_ROM_SIZE     DRIVE_ROM_SIZE

#define MAX_D64_SIZE            (174848)
#define D64_TRACKS              35

#define CORE0_EMULATION         0
#define CORE1_VIDEO             1

#endif /* BOARD_CONFIG_H */
