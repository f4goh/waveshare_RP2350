/*
 * PIO TMDS differential serialiser (adapte RP2350B GPIO 32..39 avec masques 64 bits)
 */

#ifndef DVI_SERIALISER_PIO_H
#define DVI_SERIALISER_PIO_H

#include "hardware/pio.h"

#define dvi_serialiser_wrap_target 0
#define dvi_serialiser_wrap        1

static const uint16_t dvi_serialiser_program_instructions[] = {
    0x70a1, /* 0: out pc, 1 side 2 */
    0x68a1, /* 1: out pc, 1 side 1 */
};

static const struct pio_program dvi_serialiser_program = {
    .instructions = dvi_serialiser_program_instructions,
    .length       = 2,
    .origin       = 0,
#if defined(PICO_PIO_VERSION) && (PICO_PIO_VERSION > 0)
    .used_gpio_ranges = 0x0,
#endif
};

static inline pio_sm_config dvi_serialiser_program_get_default_config(uint offset)
{
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + dvi_serialiser_wrap_target, offset + dvi_serialiser_wrap);
    sm_config_set_sideset(&c, 2, false, false);
    return c;
}

static inline void dvi_serialiser_program_init(PIO pio, uint sm, uint offset, uint data_pins)
{
    /* Masques 64 bits indispensables sur RP2350B pour GPIO >= 32 */
    pio_sm_set_pins_with_mask64(pio, sm, 2ULL << data_pins, 3ULL << data_pins);
    pio_sm_set_pindirs_with_mask64(pio, sm, ~0ULL, 3ULL << data_pins);
    pio_gpio_init(pio, data_pins);
    pio_gpio_init(pio, data_pins + 1u);

    pio_sm_config c = dvi_serialiser_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, data_pins);
    sm_config_set_out_shift(&c, true, true, 20); /* 2 symboles TMDS * 10 bits */
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, false);
}

#endif /* DVI_SERIALISER_PIO_H */
