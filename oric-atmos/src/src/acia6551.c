/*
 * Emulation MOS 6551 ACIA
 */

#include "acia6551.h"
#include <stddef.h>

static const uint32_t s_baudrates[16] = {
    0, 50, 75, 110, 134, 150, 300, 600,
    1200, 1800, 2400, 3600, 4800, 7200, 9600, 19200
};

static void acia_update_params(struct acia *acia)
{
    int wlen = 8 - ((acia->regs[ACIA_CONTROL] & ACONF_WLEN) >> 5);
    switch (wlen) {
        case 5:  acia->bitmask = 0x1Fu; break;
        case 6:  acia->bitmask = 0x3Fu; break;
        case 7:  acia->bitmask = 0x7Fu; break;
        default: acia->bitmask = 0xFFu; break;
    }

    acia->rx    = (acia->regs[ACIA_COMMAND] & ACOMF_DTR) != 0;
    acia->irqrx = acia->rx && ((acia->regs[ACIA_COMMAND] & ACOMF_IRQDIS) == 0);

    switch ((acia->regs[ACIA_COMMAND] & ACOMF_TXCON) >> 2) {
        case 1:
            acia->tx    = true;
            acia->irqtx = true;
            break;
        case 2:
            acia->tx    = true;
            acia->irqtx = false;
            break;
        default:
            acia->tx    = false;
            acia->irqtx = false;
            break;
    }

    acia->echo = (acia->regs[ACIA_COMMAND] & ACOMF_ECHO) != 0;

    acia->framebits = 1u + (uint32_t)wlen +
                      ((acia->regs[ACIA_COMMAND] >> 5) & 1u) +
                      ((acia->regs[ACIA_CONTROL] & ACONF_STOPB) ? 2u : 1u);

    acia->baudrate = s_baudrates[acia->regs[ACIA_CONTROL] & ACONF_BAUD];
    if (acia->baudrate > 0) {
        acia->framecycle = (115200u * 8680u) / (acia->baudrate * 1000u);
    } else {
        acia->framecycle = 0;
    }
}

static void acia_apply_config(struct acia *acia)
{
    if (!acia->set_config || acia->baudrate == 0) {
        return;
    }

    int  bits     = 8 - ((acia->regs[ACIA_CONTROL] & ACONF_WLEN) >> 5);
    int  stopbits = (acia->regs[ACIA_CONTROL] & ACONF_STOPB) ? 2 : 1;
    char parity   = 'N';

    if (acia->regs[ACIA_COMMAND] & 0x20u) {
        uint8_t pmode = (acia->regs[ACIA_COMMAND] >> 6) & 0x03u;
        parity = (pmode == 1u) ? 'E' : 'O';
    }

    acia->set_config(acia->baudrate, bits, parity, stopbits);
}

void acia_init(struct acia *acia)
{
    if (acia->done) {
        acia->done(acia);
    }

    acia->regs[ACIA_RXDATA]  = 0;
    acia->regs[ACIA_TXDATA]  = 0;
    acia->regs[ACIA_STATUS]  = ASTF_TXEMPTY;
    acia->regs[ACIA_COMMAND] = ACOMF_IRQDIS;
    acia->regs[ACIA_CONTROL] = 0;
    acia->cycles             = 0;

    acia_update_params(acia);
    acia_init_com(acia);
}

void acia_clock(struct acia *acia, unsigned int cycles)
{
    if ((acia->regs[ACIA_CONTROL] & ACONF_SRC) == 0 || acia->framecycle == 0) {
        return;
    }

    acia->cycles += cycles;

    if (acia->poll_rx) {
        acia->poll_rx();
    }

    while (acia->framecycle <= acia->cycles) {
        acia->cycles -= acia->framecycle;

        if (acia->tx && acia->put_byte &&
            (acia->regs[ACIA_STATUS] & ASTF_TXEMPTY) == 0) {
            if (!acia->put_byte(acia->regs[ACIA_TXDATA])) {
                acia->regs[ACIA_STATUS] |= ASTF_OVRUNERR;
            } else {
                acia->regs[ACIA_STATUS] |= ASTF_TXEMPTY;
                if (acia->irqtx) {
                    acia->regs[ACIA_STATUS] |= ASTF_IRQ;
                }
                if (acia->echo) {
                    acia->put_byte(acia->regs[ACIA_TXDATA]);
                }
            }
        }

        if (acia->rx && acia->has_byte &&
            (acia->regs[ACIA_STATUS] & ASTF_RXDATA) == 0) {
            uint8_t data = 0;
            if (acia->has_byte(&data)) {
                acia->regs[ACIA_RXDATA]  = data & acia->bitmask;
                acia->regs[ACIA_STATUS] |= ASTF_RXDATA;
                if (acia->irqrx) {
                    acia->regs[ACIA_STATUS] |= ASTF_IRQ;
                }
            }
        }
    }

    if (acia->stat) {
        acia->regs[ACIA_STATUS] = acia->stat(acia->regs[ACIA_STATUS]);
    } else {
        acia->regs[ACIA_STATUS] |= (ASTF_CARRIER | ASTF_DSR);
    }
}

void acia_write(struct acia *acia, uint16_t addr, uint8_t data)
{
    switch (addr & 3u) {
        case ACIA_RXDATA:
            acia->regs[ACIA_TXDATA]  = data & acia->bitmask;
            acia->regs[ACIA_STATUS] &= (uint8_t)~ASTF_TXEMPTY;
            break;

        case ACIA_STATUS:
            acia->regs[ACIA_COMMAND] &= ACOMF_PARITY;
            acia->regs[ACIA_COMMAND] |= ACOMF_IRQDIS;
            acia->regs[ACIA_STATUS]  &= (uint8_t)~ASTF_OVRUNERR;
            acia_update_params(acia);
            break;

        case ACIA_COMMAND:
            acia->regs[ACIA_COMMAND] = data;
            acia_update_params(acia);
            acia_apply_config(acia);
            break;

        case ACIA_CONTROL:
            acia->regs[ACIA_CONTROL] = data;
            acia_update_params(acia);
            acia_apply_config(acia);
            break;
    }
}

uint8_t acia_read(struct acia *acia, uint16_t addr)
{
    uint8_t data = acia->regs[addr & 3u];

    switch (addr & 3u) {
        case ACIA_RXDATA:
            if (acia->get_byte && acia->get_byte(&data)) {
                data &= acia->bitmask;
                acia->regs[ACIA_RXDATA] = data;
            }
            acia->regs[ACIA_STATUS] &= (uint8_t)~(ASTF_PARITYERR |
                                                  ASTF_FRAMEERR  |
                                                  ASTF_OVRUNERR  |
                                                  ASTF_RXDATA);
            break;

        case ACIA_STATUS:
            acia->regs[ACIA_STATUS] &= (uint8_t)~ASTF_IRQ;
            break;

        default:
            break;
    }

    return data;
}

bool acia_init_none(struct acia *acia)
{
    acia->done       = NULL;
    acia->stat       = NULL;
    acia->has_byte   = NULL;
    acia->get_byte   = NULL;
    acia->put_byte   = NULL;
    acia->set_config = NULL;
    acia->poll_rx    = NULL;
    return true;
}
