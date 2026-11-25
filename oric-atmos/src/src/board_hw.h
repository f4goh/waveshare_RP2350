/*
 * Initialisation materielle bas niveau RP2350-PiZero :
 *   - Correctif QE pour Flash Puya (0x85) dans une fenetre XIP SRAM
 *   - Timings QMI Flash surs avant/apres passage a 252 MHz
 *   - Initialisation et test de la PSRAM QSPI soudee APS6404L (CS1 = GPIO 47)
 */

#ifndef ORIC_BOARD_HW_H
#define ORIC_BOARD_HW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_PSRAM_CS_PIN 47U
#define BOARD_PSRAM_BASE   0x11000000UL
#define BOARD_FLASH_MAX_MHZ 66U
#define BOARD_PSRAM_MAX_MHZ 126U

typedef struct {
    bool     is_rp2350b;
    uint32_t sys_clock_hz;
    uint8_t  flash_jedec[4];
    uint32_t flash_size_bytes;
    uint8_t  flash_qe_code;      /* 0=N/A, 1=deja QE, 2=vol 50h, 3=NV 31h, 4=echec, 5=NV 01h, 6=err */
    uint8_t  flash_qe_diag[6];
    uint8_t  psram_cs_pin;
    uint8_t  psram_mfid;         /* APS6404L : 0x0D */
    uint8_t  psram_kgd;          /* APS6404L : 0x5D */
    uint32_t psram_size_bytes;   /* 8 Mo typique sur APS6404L */
    bool     psram_test_ok;
    uint32_t qmi_m0_timing;
    uint32_t qmi_m1_timing;
} BoardHwInfo;

/* Calcul pur du diviseur QMI et du RXDELAY (egalement testable sur hote) */
static inline void board_compute_qmi_timing(uint32_t clock_hz,
                                            uint32_t max_freq_hz,
                                            int     *divisor_out,
                                            int     *rxdelay_out)
{
    if (!max_freq_hz || !divisor_out || !rxdelay_out) {
        return;
    }
    int divisor = (int)((clock_hz + max_freq_hz - 1u) / max_freq_hz);
    if (divisor < 1) {
        divisor = 1;
    }
    if (divisor == 1 && clock_hz > 100000000u) {
        divisor = 2;
    }
    int rxdelay = divisor;
    if ((clock_hz / (uint32_t)divisor) > 100000000u) {
        rxdelay += 1;
    }
    *divisor_out = divisor;
    *rxdelay_out = rxdelay;
}

void     board_flash_init(void);
void     board_set_clock_252mhz(void);
uint32_t board_psram_init(uint8_t cs_pin);
uint32_t board_psram_size(void);
bool     board_psram_quick_test(void);
void     board_hw_get_info(BoardHwInfo *out);

#ifdef __cplusplus
}
#endif

#endif /* ORIC_BOARD_HW_H */
