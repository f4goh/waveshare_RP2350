/*
 * Interface video HDMI/DVI + ULA sur Waveshare RP2350-PiZero
 */

#include "video.h"
#include "config.h"
#include "hdmi_dvi.h"
#include "ula.h"
#include "pico/stdlib.h"

static Ula s_ula;

static void __not_in_flash_func(on_scanline_tmds)(
    uint16_t y_logical, uint32_t *tmds_b, uint32_t *tmds_g, uint32_t *tmds_r)
{
    if (y_logical < 8u || y_logical >= 232u) {
        return;
    }
    ula_scanline_tmds(&s_ula, (uint16_t)(y_logical - 8u), tmds_b, tmds_g, tmds_r);
}

static void __not_in_flash_func(on_end_frame)(uint32_t frame)
{
    (void)frame;
    ula_endframe(&s_ula);
}

void monitorEnable(bool enable)
{
    ula_monitor(&s_ula, enable);
}

uint8_t videoGetPattr(void)
{
    return s_ula.enMonitor ? s_ula.pattrSave : s_ula.pattr;
}

void videoSetMode(uint8_t hires)
{
    uint8_t mode = hires ? PATTR_HIRES : 0u;
    if (s_ula.enMonitor) {
        s_ula.pattrSave = mode;
    } else {
        s_ula.pattr = mode;
    }
}

void videoSetPalette(uint8_t palette)
{
    ula_set_palette(&s_ula, palette);
}

uint8_t videoGetPalette(void)
{
    return s_ula.palette;
}

void videoInit(void)
{
    ula_init(&s_ula);
    hdmi_dvi_init(on_scanline_tmds, on_end_frame);
    hdmi_dvi_launch_core1();
}
