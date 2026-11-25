/*
 * Pilote manette NES (CD4021) non bloquant sans occupation PIO
 * Brochage compatible Waveshare RP2350-PiZero Port 1 (CLK=5, DATA=6, LATCH=9)
 */

#ifndef NES_CTRL_H
#define NES_CTRL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void    nes_begin(void);
bool    nes_poll_nonblocking(uint8_t *out_bits);
uint8_t nes_read_blocking(void);
uint8_t nes_get_state_1(void);

#ifdef __cplusplus
}
#endif

#endif /* NES_CTRL_H */
