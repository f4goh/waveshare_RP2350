/*
 * main_scumm.cpp - Emulateur SCUMM v3/v4 pour Waveshare RP2350-PiZero
 *   - HDMI 640x480p60 (320x240 RGB565, Core 1 PIO0)
 *   - Audio unifie 3-en-1 : HDMI 48 kHz / I2S (GPIO 18/19/21) / PWM (GPIO 26)
 *   - Dual USB Host (Clavier + Souris + Hub + Manette) + Clavier PS/2 + Manette NES
 *   - 8 Mo PSRAM (GPIO 47) + MicroSD SPI1 (GPIO 30/31/40/43)
 */

#include <Arduino.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/time.h"

#include "board_hw.h"
#include "psram_allocator.h"
#include "hdmi_dvi.h"
#include "audio.h"
#include "input_scumm.h"
#include "scumm_osd.h"

#include "machines/scumm/scumm_machine.h"
#include "machines/scumm/scumm_new.h"

extern uint16_t g_scumm_fb[320 * 240];
extern void scumm_audio_pump_frame(void);

uint32_t butter_psram_size(void)
{
    return board_psram_size();
}

static scumm_system g_sys;

static void __not_in_flash_func(scumm_hdmi_scanline_cb)(uint16_t y_logical,
                                                        uint32_t *tmds_b,
                                                        uint32_t *tmds_g,
                                                        uint32_t *tmds_r)
{
    const uint16_t *row = &g_scumm_fb[(size_t)y_logical * 320u];
    hdmi_encode_scanline_rgb565(row, tmds_b, tmds_g, tmds_r);
}

static void __not_in_flash_func(scumm_hdmi_endframe_cb)(uint32_t frame_count)
{
    (void)frame_count;
}

static void idle_error_loop(uint16_t color565, const char *title, const char *l1, const char *l2)
{
    while (1) {
        input_scumm_poll(nullptr);
        scumm_osd_error_screen(g_scumm_fb, color565, title, l1, l2);
        if (scumm_osd_is_open()) {
            scumm_osd_render(g_scumm_fb);
        }
        for (int i = 0; i < 800; i++) {
            audioPushSidSample(0, 0);
        }
        sleep_ms(16);
    }
}

int main(void)
{
    /* 1. Hardware Waveshare RP2350-PiZero (Flash Puya + 252 MHz + PSRAM 8 Mo) */
    board_flash_init();
    board_set_clock_252mhz();
    initVariant();
    board_psram_init(BOARD_PSRAM_CS_PIN);
    psram_reset();

    memset(g_scumm_fb, 0, sizeof(g_scumm_fb));

    /* 2. Stabilisation alimentation USB */
    sleep_ms(150);

    /* 3. HDMI / DVI Core 1 */
    hdmi_dvi_init(scumm_hdmi_scanline_cb, scumm_hdmi_endframe_cb);
    hdmi_dvi_launch_core1();

    /* 4. Audio unifie 3-en-1 (HDMI / I2S / PWM) */
    audioInit(48000u);

    /* 5. Entrees (USB Hub/Souris/Clavier + PS/2 + Manette NES) */
    input_scumm_init();

    /* 6. Initialisation moteur SCUMM + MicroSD */
    memset(&g_sys, 0, sizeof(g_sys));
    scumm_init(&g_sys);

    uint16_t err_color = 0;
    if (!scumm_load(&g_sys, &err_color)) {
        const char *msg1 = "VERIFIEZ LA CARTE MICROSD";
        const char *msg2 = "DOSSIER /CART/*.SCUMM REQUIS";
        if (g_sys.boot_error == SCUMM_BOOT_NO_CARD) {
            msg1 = "AUCUNE CARTE MICROSD DETECTEE";
            msg2 = "INSEREZ UNE CARTE FAT32";
        } else if (g_sys.boot_error == SCUMM_BOOT_NO_MARKER) {
            msg1 = "AUCUN FICHIER .SCUMM IN /CART";
            msg2 = "EX: /CART/LOOM.SCUMM";
        } else if (g_sys.boot_error == SCUMM_BOOT_NO_FOLDER) {
            msg1 = "DOSSIER JEU VIDE OU ABSENT";
            msg2 = g_sys.folder;
        } else if (g_sys.boot_error == SCUMM_BOOT_NO_MEMORY) {
            msg1 = "ERREUR MEMOIRE PSRAM (2 MO)";
            msg2 = "VERIFIEZ PSRAM GPIO 47";
        }
        idle_error_loop(err_color, scumm_boot_error_text(g_sys.boot_error), msg1, msg2);
    }

    if (!scumm_start(&g_sys, "adlib", true, &err_color)) {
        idle_error_loop(err_color, "ERREUR DEMARRAGE SCUMM", scumm_engine_error(), "F4 = MENU CONFIG");
    }

    /* 7. Boucle principale 60 Hz */
    const uint32_t FRAME_US = 16667u;
    uint64_t next_us = to_us_since_boot(get_absolute_time());

    while (1) {
        input_scumm_poll(&g_sys);

        if (scumm_osd_is_open()) {
            scumm_osd_render(g_scumm_fb);
            for (int i = 0; i < 800; i++) {
                audioPushSidSample(0, 0);
            }
        } else {
            scumm_run_frame(&g_sys);
            scumm_audio_pump_frame();
        }

        next_us += FRAME_US;
        uint64_t now_us = to_us_since_boot(get_absolute_time());
        if (now_us < next_us) {
            uint32_t wait_us = (uint32_t)(next_us - now_us);
            if (wait_us > 1000u) {
                sleep_us(wait_us - 1000u);
            }
            while (to_us_since_boot(get_absolute_time()) < next_us) {
                tight_loop_contents();
            }
        } else if (now_us > next_us + FRAME_US * 2u) {
            next_us = now_us;
        }
    }

    return 0;
}
