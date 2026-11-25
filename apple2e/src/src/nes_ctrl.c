/*
 * Pilote manette NES (CD4021) sur GPIO 5 (CLK), 6 (DATA), 9 (LATCH)
 * Actif bas identique a nes.pio : bit7=A, bit6=B, bit5=Sel, bit4=Start, bit3..0=U,D,L,R
 */

#include "nes_ctrl.h"
#include "config.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#define NES_POLL_PERIOD_US 16667U /* ~60 Hz */

typedef enum {
    NES_IDLE = 0,
    NES_LATCH_HIGH,
    NES_SAMPLE_BIT,
    NES_CLOCK_HIGH
} NesStep;

static NesStep  s_step       = NES_IDLE;
static uint8_t  s_bitIdx     = 0;
static uint8_t  s_accum      = 0xFFu;
static uint8_t  s_nesState1  = 0xFFu;
static uint32_t s_lastPollUs = 0;

void nes_begin(void)
{
    gpio_init(PIN_NES_CLOCK);
    gpio_set_dir(PIN_NES_CLOCK, GPIO_OUT);
    gpio_put(PIN_NES_CLOCK, 0);

    gpio_init(PIN_NES_LATCH);
    gpio_set_dir(PIN_NES_LATCH, GPIO_OUT);
    gpio_put(PIN_NES_LATCH, 0);

    gpio_init(PIN_NES_DATA);
    gpio_set_dir(PIN_NES_DATA, GPIO_IN);
    gpio_pull_up(PIN_NES_DATA);

    s_step       = NES_IDLE;
    s_bitIdx     = 0;
    s_accum      = 0xFFu;
    s_nesState1  = 0xFFu;
    s_lastPollUs = time_us_32();
}

uint8_t nes_get_state_1(void)
{
    return s_nesState1;
}

bool __not_in_flash_func(nes_poll_nonblocking)(uint8_t *out_bits)
{
    switch (s_step) {
        case NES_IDLE: {
            uint32_t now = time_us_32();
            if ((uint32_t)(now - s_lastPollUs) < NES_POLL_PERIOD_US) {
                return false;
            }
            s_lastPollUs = now;
            gpio_put(PIN_NES_CLOCK, 0);
            gpio_put(PIN_NES_LATCH, 1);
            s_accum  = 0;
            s_bitIdx = 0;
            s_step   = NES_LATCH_HIGH;
            return false;
        }

        case NES_LATCH_HIGH:
            gpio_put(PIN_NES_LATCH, 0);
            s_step = NES_SAMPLE_BIT;
            return false;

        case NES_SAMPLE_BIT: {
            uint8_t d = gpio_get(PIN_NES_DATA) ? 1u : 0u;
            s_accum = (uint8_t)((s_accum << 1) | d);
            s_bitIdx++;
            gpio_put(PIN_NES_CLOCK, 1);
            s_step = NES_CLOCK_HIGH;
            return false;
        }

        case NES_CLOCK_HIGH:
            gpio_put(PIN_NES_CLOCK, 0);
            if (s_bitIdx >= 8u) {
                s_nesState1 = s_accum;
                s_step      = NES_IDLE;
                if (out_bits) {
                    *out_bits = s_nesState1;
                }
                return true;
            }
            s_step = NES_SAMPLE_BIT;
            return false;
    }

    return false;
}

uint8_t nes_read_blocking(void)
{
    /* Remet la FSM au repos si une capture asynchrone etait en cours */
    gpio_put(PIN_NES_CLOCK, 0);
    gpio_put(PIN_NES_LATCH, 0);
    s_step = NES_IDLE;

    gpio_put(PIN_NES_LATCH, 1);
    busy_wait_us_32(12);
    gpio_put(PIN_NES_LATCH, 0);
    busy_wait_us_32(6);

    uint8_t val = 0;
    for (uint8_t i = 0; i < 8u; i++) {
        uint8_t d = gpio_get(PIN_NES_DATA) ? 1u : 0u;
        val = (uint8_t)((val << 1) | d);
        gpio_put(PIN_NES_CLOCK, 1);
        busy_wait_us_32(6);
        gpio_put(PIN_NES_CLOCK, 0);
        busy_wait_us_32(6);
    }

    s_nesState1  = val;
    s_lastPollUs = time_us_32();
    return val;
}
