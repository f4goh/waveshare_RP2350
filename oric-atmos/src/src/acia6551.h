/*
 * Emulation MOS 6551 ACIA (RS232)
 */

#ifndef ACIA6551_H
#define ACIA6551_H

#include <stdbool.h>
#include <stdint.h>

enum {
    ACIA_RXDATA = 0,
    ACIA_STATUS,
    ACIA_COMMAND,
    ACIA_CONTROL,
    ACIA_TXDATA,
    ACIA_LAST
};

/* Command register bits */
#define ACOMF_DTR     (1u << 0)
#define ACOMF_IRQDIS  (1u << 1)
#define ACOMF_TXCON   0x0Cu
#define ACOMF_ECHO    (1u << 4)
#define ACOMF_PARITY  0xE0u

/* Control register bits */
#define ACONF_BAUD    0x0Fu
#define ACONF_SRC     (1u << 4)
#define ACONF_WLEN    0x60u
#define ACONF_STOPB   (1u << 7)

/* Status register bits */
#define ASTF_PARITYERR (1u << 0)
#define ASTF_FRAMEERR  (1u << 1)
#define ASTF_OVRUNERR  (1u << 2)
#define ASTF_RXDATA    (1u << 3)
#define ASTF_TXEMPTY   (1u << 4)
#define ASTF_CARRIER   (1u << 5)
#define ASTF_DSR       (1u << 6)
#define ASTF_IRQ       (1u << 7)

struct acia {
    uint8_t  regs[ACIA_LAST];
    uint32_t cycles;
    uint32_t baudrate;
    uint32_t framebits;
    uint32_t framecycle;

    uint8_t  bitmask;
    bool     rx;
    bool     irqrx;
    bool     tx;
    bool     irqtx;
    bool     echo;

    uint8_t (*stat)(uint8_t stat);
    bool    (*has_byte)(uint8_t *data);
    bool    (*get_byte)(uint8_t *data);
    bool    (*put_byte)(uint8_t data);
    void    (*done)(struct acia *acia);
    bool    (*set_config)(uint32_t baud, int bits, char parity, int stopbits);
    void    (*poll_rx)(void);
};

void    acia_init(struct acia *acia);
void    acia_clock(struct acia *acia, unsigned int cycles);
void    acia_write(struct acia *acia, uint16_t addr, uint8_t data);
uint8_t acia_read(struct acia *acia, uint16_t addr);
bool    acia_init_none(struct acia *acia);
bool    acia_init_com(struct acia *acia);

static inline bool acia_irq_pending(const struct acia *acia)
{
    return (acia->regs[ACIA_STATUS] & ASTF_IRQ) != 0u;
}

#endif /* ACIA6551_H */
