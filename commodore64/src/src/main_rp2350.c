/*
 *  main_rp2350.c - Main entry point for C64 Emulator on Waveshare RP2350-PiZero
 *
 *  Core 0: Emulation 6510 CPU, VIC-II, SID, CIA1/2, 1541 IEC, Entrees, Menu OSD
 *  Core 1: Moteur HDMI / DVI 640x480p60 + Packets Audio HDMI L-PCM 48 kHz
 */

#include "board_config.h"
#include "board_hw.h"
#include "debug_log.h"
#include "hdmi_dvi.h"
#include "audio.h"
#include "psram_allocator.h"
#include "c64_config_menu.h"
#include "disk_ui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"

#include "sdcard/sdcard.h"
#include "fatfs/ff.h"

#ifdef __cplusplus
extern "C" {
#endif

void initVariant(void);
void c64_init(void);
void c64_reset(void);
bool c64_run_frame(void);
uint8_t *c64_get_framebuffer(void);
void c64_set_drive_leds(int l0, int l1, int l2, int l3);
void c64_show_notification(const char *msg);

void input_rp2350_init(void);
void input_reset_keys(void);
void input_rp2350_poll(uint8_t *key_matrix, uint8_t *rev_matrix, uint8_t *joystick);
void input_rp2350_poll_no_c64_acts(void);

void disk_loader_init(void);
void disk_loader_scan(void);

extern uint8_t g_pixels[384 * 272];

#ifdef __cplusplus
}
#endif

static volatile bool g_emulator_ready = false;
static volatile bool g_quit_requested = false;

/*
 * Callback ligne par ligne pour Core 1 (HDMI TMDS) :
 * y_logical = 0..239 (ecran complet 320x240 doublé 2x en 640x480p)
 */
static void __not_in_flash_func(c64_hdmi_scanline_cb)(uint16_t y_logical,
                                                      uint32_t *tmds_b,
                                                      uint32_t *tmds_g,
                                                      uint32_t *tmds_r)
{
    const uint8_t *row = g_pixels + C64_CROP_LEFT + (y_logical + C64_CROP_TOP) * C64_DISPLAY_WIDTH;
    hdmi_encode_scanline_indexed(row, tmds_b, tmds_g, tmds_r);
}

static void __not_in_flash_func(c64_hdmi_endframe_cb)(uint32_t frame_count)
{
    (void)frame_count;
}

static void emulator_main_loop(void)
{
    g_emulator_ready = true;

    const uint32_t FRAME_TIME_US = 20000; // PAL 50 Hz = 20 ms
    uint64_t next_frame_time = to_us_since_boot(get_absolute_time());

    while (!g_quit_requested) {
        if (c64_config_menu_is_open()) {
            input_rp2350_poll_no_c64_acts();
            c64_config_menu_render();
        } else if (disk_ui_is_visible()) {
            input_rp2350_poll_no_c64_acts();
            disk_ui_render();
        } else {
            c64_run_frame();
        }

        next_frame_time += FRAME_TIME_US;
        uint64_t now_us = to_us_since_boot(get_absolute_time());

        if (now_us < next_frame_time) {
            uint32_t wait_us = (uint32_t)(next_frame_time - now_us);
            if (wait_us > 1000) {
                sleep_us(wait_us - 1000);
            }
            while (to_us_since_boot(get_absolute_time()) < next_frame_time) {
                tight_loop_contents();
            }
        } else if (now_us > next_frame_time + FRAME_TIME_US * 2) {
            next_frame_time = now_us;
        }
    }
}

int main(void)
{
    // 1. Initialisation hardware Waveshare RP2350-PiZero (Puya Flash QE + 252 MHz + PSRAM)
    board_flash_init();
    board_set_clock_252mhz();
    initVariant();
    board_psram_init(BOARD_PSRAM_CS_PIN);
    psram_reset();

    // 2. Nettoyage complet du framebuffer pour eviter les artefacts au redemarrage
    memset(g_pixels, 0, sizeof(g_pixels));

    // 3. Delai de stabilisation alimentation VBUS USB
    sleep_ms(50);

    // 4. Initialisation du coeur de l'emulateur C64
    c64_init();

    // 5. Initialisation HDMI / DVI Core 1
    hdmi_dvi_init(c64_hdmi_scanline_cb, c64_hdmi_endframe_cb);
    hdmi_dvi_launch_core1();

    // 6. Entrees (Dual USB Host, PS/2, NES pad)
    input_rp2350_init();

    // 7. MicroSD (SPI1 hardware, montage differe sans blocage si absente)
    static FATFS fatfs;
    f_mount(&fatfs, "", 0);
    disk_loader_init();

    // 8. Audio unifie (HDMI 48 kHz / I2S / PWM)
    audioInit(SAMPLE_FREQUENCY);

    // 9. Boucle principale C64
    emulator_main_loop();

    return 0;
}
