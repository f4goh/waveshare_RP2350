/*
 * main_rp2350.c - Apple IIe Emulator (MII) on Waveshare RP2350-PiZero
 *
 * Core 0: CPU 65C02 (1.023 MHz), Video scaling, Disk II / SmartPort, Audio, Inputs, OSD
 * Core 1: HDMI / DVI 640x480p60 + HDMI Audio Data Islands L-PCM 48 kHz
 */

#include "pico/stdlib.h"
#include "pico/time.h"

#include "board_config.h"
#include "board_hw.h"
#include "hdmi_dvi.h"
#include "audio.h"
#include "apple_audio.h"
#include "apple_config_menu.h"
#include "psram_allocator.h"
#include "fatfs/ff.h"

#include "mii.h"
#include "mii_sw.h"
#include "mii_bank.h"
#include "mii_rom.h"
#include "mii_video.h"
#include "mii_65c02.h"
#include "mii_slot.h"
#include "mii_speaker.h"
#include "disk_loader.h"
#include "disk_ui.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void initVariant(void);
void input_rp2350_init(void);
void input_rp2350_poll_frame(void);

extern const uint8_t mii_rom_iiee[16384];
extern uint8_t mii_rom_iiee_video[4096];

#ifdef __cplusplus
}
#endif

mii_t g_mii;
uint8_t g_apple_fb[160 * 240] __aligned(4);

static mii_rom_t s_main_rom_desc = {
    .name        = "iiee",
    .class       = "main",
    .description = "Apple IIe Enhanced ROM",
    .rom         = NULL,
    .len         = 16384,
};

static mii_rom_t s_char_rom_desc = {
    .name        = "iiee_video",
    .class       = "video",
    .description = "Apple IIe Video ROM",
    .rom         = NULL,
    .len         = 4096,
};

static const uint32_t s_apple2_rgb888[16] = {
    0x000000, /* Black */
    0xDD0033, /* Magenta */
    0x000099, /* Dark Blue */
    0xDD22DD, /* Purple */
    0x007722, /* Dark Green */
    0x555555, /* Grey 1 */
    0x2222FF, /* Medium Blue */
    0x66AAFF, /* Light Blue */
    0x885500, /* Brown */
    0xFF6600, /* Orange */
    0xAAAAAA, /* Grey 2 */
    0xFF9988, /* Pink */
    0x11DD00, /* Light Green */
    0xFFFF00, /* Yellow */
    0x44FF99, /* Aqua */
    0xFFFFFF, /* White */
};

void apple_set_monitor_palette(uint8_t mode)
{
    for (int i = 0; i < 16; i++) {
        uint32_t c = s_apple2_rgb888[i];
        if (mode == 1u || mode == 2u) {
            uint32_t r = (c >> 16) & 0xFFu;
            uint32_t g = (c >> 8)  & 0xFFu;
            uint32_t b = c & 0xFFu;
            uint32_t luma = (r * 77u + g * 150u + b * 29u) >> 8;
            if (mode == 1u) {
                /* Green P31 Monochrome */
                c = ((luma >> 3) << 16) | (luma << 8) | (luma >> 3);
            } else {
                /* Amber P34 Monochrome */
                c = (luma << 16) | (((luma * 176u) >> 8) << 8) | (luma >> 4);
            }
        }
        graphics_set_palette((uint8_t)i, c);
    }
    for (int i = 16; i < 256; i++) {
        uint32_t gray = (((uint32_t)i & 0xE0u) << 16) |
                        (((uint32_t)i & 0x1Cu) << 11) |
                        (((uint32_t)i & 0x03u) << 6);
        graphics_set_palette((uint8_t)i, gray);
    }
}

/* Stubs required by MII Apple IIe core */
void mii_speaker_click(mii_speaker_t *speaker)
{
    (void)speaker;
    apple_audio_speaker_click(g_mii.cpu.total_cycle);
}

int mii_cpu_disasm_one(char *buf, size_t buflen, mii_cpu_t *cpu,
                       uint8_t (*read_byte)(void*, uint16_t), void *param)
{
    (void)buf; (void)buflen; (void)cpu; (void)read_byte; (void)param;
    return 0;
}

uint32_t butter_psram_size(void)
{
    return board_psram_size();
}

volatile int lock_y = -1;

extern bool input_is_show_speed(void);
extern bool input_is_speed_unlocked(void);
extern void draw_string(uint8_t *fb, int fb_width, int x, int y, const char *str, uint8_t color);

bool ps2kbd_is_show_speed(void)
{
    return input_is_show_speed();
}

void apple_keypress(uint8_t key)
{
    mii_keypress(&g_mii, key);
}

void apple_reset_req(bool cold)
{
    mii_reset(&g_mii, cold);
}

void apple_key_repeat_tick(uint8_t key)
{
    mii_bank_t *sw = &g_mii.bank[MII_BANK_SW];
    if (!(mii_bank_peek(sw, 0xC010) & 0x80)) {
        mii_keypress(&g_mii, key);
    }
}

void apple_set_paddles(uint8_t x, uint8_t y, bool btn0, bool btn1, bool btn2)
{
    mii_bank_t *sw = &g_mii.bank[MII_BANK_SW];
    mii_bank_poke(sw, 0xC061, btn0 ? 0x80 : 0x00);
    mii_bank_poke(sw, 0xC062, btn1 ? 0x80 : 0x00);
    mii_bank_poke(sw, 0xC063, btn2 ? 0x80 : 0x00);
    g_mii.analog.v[0].value = x;
    g_mii.analog.v[1].value = y;
}

static void __not_in_flash_func(apple_hdmi_scanline_cb)(uint16_t y_logical,
                                                        uint32_t *tmds_b,
                                                        uint32_t *tmds_g,
                                                        uint32_t *tmds_r)
{
    const uint8_t *row = g_apple_fb + (size_t)y_logical * 160u;
    hdmi_encode_scanline_4bpp(row, tmds_b, tmds_g, tmds_r);
}

static void __not_in_flash_func(apple_hdmi_endframe_cb)(uint32_t frame_count)
{
    (void)frame_count;
}

static void apple_init_machine(void)
{
    memset(&g_mii, 0, sizeof(g_mii));

    s_main_rom_desc.rom = mii_rom_iiee;
    s_main_rom_desc.len = 16384;
    mii_rom_register(&s_main_rom_desc);

    mii_rom_t *vrom = mii_rom_get("iiee_video");
    if (vrom) {
        vrom->rom = mii_rom_iiee_video;
        vrom->len = 4096;
    } else {
        s_char_rom_desc.rom = mii_rom_iiee_video;
        s_char_rom_desc.len = 4096;
        mii_rom_register(&s_char_rom_desc);
    }

    mii_init(&g_mii);
    g_mii.video.an3_mode = 1;
    g_mii.video.rom = mii_rom_get("iiee_video");

    mii_slot_drv_register(&g_mii, 6, "disk2");
    mii_slot_drv_register(&g_mii, 5, "smartport");
    disk_ui_init_with_emulator(&g_mii, 6);

    mii_reset(&g_mii, true);
    mii_run_cycles(&g_mii, 1000000);
    apple_audio_sync(g_mii.cpu.total_cycle);
}

static void emulator_main_loop(void)
{
    const uint32_t FRAME_TIME_US    = 16667; /* 60 Hz NTSC */
    const uint32_t CYCLES_PER_FRAME = 17030; /* 1.023 MHz / 60 Hz */

    uint64_t next_frame_time = to_us_since_boot(get_absolute_time());
    uint64_t speed_last_us   = next_frame_time;
    uint64_t speed_last_cyc  = g_mii.cpu.total_cycle;
    uint32_t cpu_khz         = 1023;
    uint32_t cpu_pct         = 100;

    while (1) {
        input_rp2350_poll_frame();

        if (apple_config_menu_is_open()) {
            apple_config_menu_render();
            apple_audio_sync(g_mii.cpu.total_cycle);
            for (int i = 0; i < 800; i++) audioPushSidSample(0, 0);
            speed_last_cyc = g_mii.cpu.total_cycle;
            speed_last_us  = to_us_since_boot(get_absolute_time());
        } else if (disk_ui_is_visible()) {
            disk_ui_render(g_apple_fb, HDMI_WIDTH, HDMI_HEIGHT);
            apple_audio_sync(g_mii.cpu.total_cycle);
            for (int i = 0; i < 800; i++) audioPushSidSample(0, 0);
            speed_last_cyc = g_mii.cpu.total_cycle;
            speed_last_us  = to_us_since_boot(get_absolute_time());
        } else {
            uint64_t c0 = g_mii.cpu.total_cycle;
            if (input_is_speed_unlocked()) {
                uint64_t target_us = next_frame_time + FRAME_TIME_US;
                for (int batch = 0; batch < 16; batch++) {
                    mii_run_cycles(&g_mii, CYCLES_PER_FRAME);
                    if (to_us_since_boot(get_absolute_time()) >= target_us) {
                        break;
                    }
                }
                apple_audio_sync(g_mii.cpu.total_cycle);
                for (int i = 0; i < 800; i++) audioPushSidSample(0, 0);
            } else {
                mii_run_cycles(&g_mii, CYCLES_PER_FRAME);
                uint64_t c1 = g_mii.cpu.total_cycle;
                apple_audio_render_frame(c0, c1);
            }

            mii_video_scale_to_hdmi(&g_mii.video, g_apple_fb);
            if (ps2kbd_is_show_speed()) {
                uint64_t now_s = to_us_since_boot(get_absolute_time());
                uint64_t dt_us = now_s - speed_last_us;
                if (dt_us >= 500000ULL) {
                    uint64_t dc = g_mii.cpu.total_cycle - speed_last_cyc;
                    cpu_khz = (uint32_t)((dc * 1000ULL) / dt_us);
                    cpu_pct = (uint32_t)((cpu_khz * 100ULL) / 1023ULL);
                    speed_last_us  = now_s;
                    speed_last_cyc = g_mii.cpu.total_cycle;
                }
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "CPU %4lu kHz %3lu%%",
                         (unsigned long)cpu_khz, (unsigned long)cpu_pct);
                memset(g_apple_fb, 0, 320 * 8 / 2);
                draw_string(g_apple_fb, 320, 0, 0, tmp, 15);
            }
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
    /* 1. Hardware Waveshare RP2350-PiZero (Puya Flash QE + 252 MHz + PSRAM) */
    board_flash_init();
    board_set_clock_252mhz();
    initVariant();
    board_psram_init(BOARD_PSRAM_CS_PIN);
    psram_reset();

    /* 2. Nettoyage framebuffer */
    memset(g_apple_fb, 0, sizeof(g_apple_fb));

    /* 3. Stabilisation alimentation USB */
    sleep_ms(150);

    /* 4. Initialisation Apple IIe */
    apple_init_machine();

    /* 5. HDMI / DVI Core 1 + Palette Apple IIe */
    hdmi_dvi_init(apple_hdmi_scanline_cb, apple_hdmi_endframe_cb);
    apple_set_monitor_palette(0u);
    hdmi_dvi_launch_core1();

    /* 6. Entrees (Dual USB Host, PS/2, NES) */
    input_rp2350_init();

    /* 7. MicroSD (SPI1, montage differe sans blocage) */
    static FATFS fatfs;
    f_mount(&fatfs, "", 0);
    disk_loader_init();

    /* 8. Audio unifie 3-en-1 (HDMI 48 kHz / I2S / PWM) */
    apple_audio_init();
    audioInit(SAMPLE_FREQUENCY);

    /* 9. Boucle principale 60 Hz */
    emulator_main_loop();

    return 0;
}
