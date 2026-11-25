/*
 * Emulation MOS 6522 VIA + AY-3-8912 + lecture matrice clavier Oric
 * Corrections par rapport a versionPerso :
 * - Support scan multi-colonnes (ex: 0x00 "any key") sur clavier virtuel et matrice GPIO
 * - Correction CB2 vs CA2 dans via_main_w_iorb
 * - Front descendant strict sur strobe imprimante PB4
 */

#include "via6522.h"
#include "audio.h"
#include "config.h"
#include "oric_keyboard.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/time.h"

#include <string.h>

static Ay8912Bus s_ay;
static uint8_t   s_keyFound  = 0;
static uint8_t   s_lignesMem = 0;

uint8_t viaSize(void)
{
    return (uint8_t)sizeof(ViaSave);
}

void viaSaveState(const struct via *v, uint8_t *dst)
{
    if (v && dst) {
        memcpy(dst, v, sizeof(ViaSave));
    }
}

void viaLoadState(struct via *v, const uint8_t *src)
{
    if (v && src) {
        memcpy(v, src, sizeof(ViaSave));
    }
}

static void __not_in_flash_func(ay_modeset)(struct via *v)
{
    switch (s_ay.bmode) {
        case (AYBMF_BDIR | AYBMF_BC1):
            s_ay.creg = v->read_port_a(v);
            audioWritePsg0(0, s_ay.creg);
            break;

        case AYBMF_BDIR: {
            uint8_t data_bus = v->read_port_a(v);
            audioWritePsg0(1, data_bus);
            if (s_ay.creg == 14u) {
                s_ay.colonnes = data_bus;
            }
            break;
        }

        case AYBMF_BC1: {
            uint8_t val_lue = audioReadPsg0();
            v->write_port_a(v, 0xFFu, val_lue);
            break;
        }

        default:
            break;
    }
}

static void ay_set_bcmode(struct via *v, uint8_t bc1, uint8_t bdir)
{
    s_ay.bmode = (uint8_t)((bc1 ? AYBMF_BC1 : 0u) | (bdir ? AYBMF_BDIR : 0u));
    ay_modeset(v);
}

static void ay_set_bc1(struct via *v, uint8_t state)
{
    if (state) s_ay.bmode |= AYBMF_BC1;
    else       s_ay.bmode &= (uint8_t)~AYBMF_BC1;
    ay_modeset(v);
}

static void ay_set_bdir(struct via *v, uint8_t state)
{
    if (state) s_ay.bmode |= AYBMF_BDIR;
    else       s_ay.bmode &= (uint8_t)~AYBMF_BDIR;
    ay_modeset(v);
}

static inline void via_check_irq(struct via *v)
{
    if ((v->ier & v->ifr) & 0x7Fu) {
        v->cpuIrq |= v->irqbit;
        v->ifr    |= 0x80u;
    } else {
        v->cpuIrq &= (uint8_t)~v->irqbit;
        v->ifr    &= 0x7Fu;
    }
}

static inline void via_set_irq(struct via *v, uint8_t bit)
{
    v->ifr |= bit;
    if ((v->ier & v->ifr) & 0x7Fu) {
        v->ifr |= 0x80u;
    }
    if ((v->ier & bit) == 0u) {
        return;
    }
    v->cpuIrq |= v->irqbit;
}

static inline void via_clr_irq(struct via *v, uint8_t bit)
{
    if (bit & VIRQF_CA1) v->iral = v->ira;
    if (bit & VIRQF_CA2) v->irbl = v->irb;

    v->ifr &= (uint8_t)~bit;
    if (((v->ier & v->ifr) & 0x7Fu) == 0u) {
        v->cpuIrq &= (uint8_t)~v->irqbit;
        v->ifr    &= 0x7Fu;
    }
}

static void via_main_w_iorb(struct via *v, uint8_t oldorb)
{
    /* Strobe imprimante sur front descendant de PB4 */
    if (((oldorb & v->ddrb & 0x10u) != 0u) && ((v->orb & v->ddrb & 0x10u) == 0u)) {
        if (uart_is_enabled(uart0)) {
            uart_putc(uart0, (char)v->ora);
        }
    }

    if ((v->pcr & PCRF_CB2CON) == 0x80u) {
        ay_set_bdir(v, 0);
    }
    ay_modeset(v);

    s_lignesMem = v->orb & 0x07u;
}

static void via_main_w_iora(struct via *v)
{
    if ((v->pcr & PCRF_CA2CON) == 0x08u) {
        ay_set_bc1(v, 0);
    }
    ay_modeset(v);
}

static void via_main_w_iora2(struct via *v)
{
    ay_modeset(v);
}

static void via_main_w_ddrb(struct via *v)
{
    ay_modeset(v);
}

static void via_main_w_pcr(struct via *v)
{
    bool updateay = false;
    if ((v->pcr & PCRF_CA2CON) == 0x0Cu || (v->pcr & PCRF_CA2CON) == 0x0Eu) {
        updateay = true;
    }
    if ((v->pcr & PCRF_CB2CON) == 0xC0u || (v->pcr & PCRF_CB2CON) == 0xE0u) {
        updateay = true;
    }
    if (updateay) {
        ay_set_bcmode(v, v->ca2, v->cb2);
    }
    ay_modeset(v);
}

static void via_main_w_ca2ext(struct via *v)
{
    if ((v->pcr & PCRF_CA2CON) <= 0x06u) {
        ay_set_bc1(v, v->ca2);
    }
    ay_modeset(v);
}

static void via_main_w_cb2ext(struct via *v)
{
    if ((v->pcr & PCRF_CB2CON) <= 0x60u) {
        ay_set_bdir(v, v->cb2);
    }
    ay_modeset(v);
}

static void via_main_r_iora(struct via *v)
{
    ay_modeset(v);
    if ((v->pcr & PCRF_CA2CON) == 0x08u) {
        ay_set_bc1(v, 0);
    }
    ay_modeset(v);
}

static void via_main_r_iora2(struct via *v)
{
    ay_modeset(v);
}

static bool __not_in_flash_func(match_key_state)(const OricKeyState *st, uint8_t ligne, uint8_t active_cols)
{
    for (uint8_t i = 0; i < st->count; i++) {
        if (st->keys[i].ligne == ligne &&
            ((active_cols & (uint8_t)~st->keys[i].colonne) != 0u)) {
            return true;
        }
    }
    return false;
}

static void __not_in_flash_func(via_main_r_iorb)(struct via *v)
{
    if ((v->pcr & PCRF_CB2CON) == 0x80u) {
        ay_set_bdir(v, 0);
    }
    ay_modeset(v);

    uint8_t lignes      = s_lignesMem;
    uint8_t colonnes    = s_ay.colonnes;
    uint8_t active_cols = (uint8_t)(~colonnes);

    /* Clavier USB HID + PS/2 + Manette NES : detection multi-colonnes par masque de bits */
    if (match_key_state(&kbdState, lignes, active_cols) ||
        match_key_state(&nesState, lignes, active_cols)) {
        s_keyFound = 1u;
    } else {
        s_keyFound = 0u;
    }
}

static void via_main_ca2pulsed(struct via *v)
{
    ay_set_bc1(v, 1);
    ay_modeset(v);
}

static void via_main_cb2pulsed(struct via *v)
{
    ay_set_bdir(v, v->cb2);
    ay_modeset(v);
}

static uint8_t via_read_porta(struct via *v)
{
    return v->ora & v->ddra;
}

static uint8_t via_read_portb(struct via *v)
{
    return (v->orb & v->ddrb) & 0x7Fu;
}

static void via_main_write_porta(struct via *v, uint8_t mask, uint8_t data)
{
    v->ira = (uint8_t)((v->ira & ~mask) | (data & mask));
    if (!(v->acr & ACRF_PALATCH) || !(v->ifr & VIRQF_CA1)) {
        v->iral = v->ira;
    }
}

static void via_write_portb(struct via *v, uint8_t mask, uint8_t data)
{
    uint8_t lastpb6 = v->irb & 0x40u;
    v->irb = (uint8_t)((v->irb & ~mask) | (data & mask));

    if (!(v->acr & ACRF_PBLATCH) || !(v->ifr & VIRQF_CB1)) {
        v->irbl = v->irb;
    }

    if ((v->acr & ACRF_T2CON) == 0u || !lastpb6 || (v->irb & 0x40u)) {
        return;
    }

    v->t2c--;
    if (v->t2run && v->t2c == 0u) {
        via_set_irq(v, VIRQF_T2);
        v->t2run = false;
    }
}

void via_init(struct via *v)
{
    memset(v, 0, sizeof(*v));
    memset(&s_ay, 0, sizeof(s_ay));
    s_keyFound  = 0;
    s_lignesMem = 0;

    v->irb    = 0xFFu;
    v->ira    = 0xFFu;
    v->irbl   = 0xFFu;
    v->iral   = 0xFFu;
    v->irqbit = 1u;

    v->w_iorb       = via_main_w_iorb;
    v->w_iora       = via_main_w_iora;
    v->w_iora2      = via_main_w_iora2;
    v->w_ddrb       = via_main_w_ddrb;
    v->w_pcr        = via_main_w_pcr;
    v->w_ca2ext     = via_main_w_ca2ext;
    v->w_cb2ext     = via_main_w_cb2ext;
    v->r_iora       = via_main_r_iora;
    v->r_iora2      = via_main_r_iora2;
    v->r_iorb       = via_main_r_iorb;
    v->ca2pulsed    = via_main_ca2pulsed;
    v->cb2pulsed    = via_main_cb2pulsed;
    v->cb2shifted   = via_main_cb2pulsed;
    v->read_port_a  = via_read_porta;
    v->read_port_b  = via_read_portb;
    v->write_port_a = via_main_write_porta;
    v->write_port_b = via_write_portb;
}

void __not_in_flash_func(via_clock)(struct via *v, unsigned int cycles)
{
    unsigned int crem;
    if (!cycles) {
        return;
    }

    if (v->ca2pulse) {
        v->ca2 = 1;
        if (v->ca2pulsed) v->ca2pulsed(v);
        v->ca2pulse = false;
    }

    if (v->cb2pulse) {
        v->cb2 = 1;
        if (v->cb2pulsed) v->cb2pulsed(v);
        v->cb2pulse = false;
    }

    /* Timer 1 */
    switch (v->acr & ACRF_T1CON) {
        case 0x00u:
        case 0x80u:
            if (v->t1run && cycles > v->t1c) {
                via_set_irq(v, VIRQF_T1);
                if (v->acr & 0x80u) {
                    v->orb |= 0x80u;
                    if (v->orbchange) v->orbchange(v);
                }
                v->t1run = false;
            }
            v->t1c -= (uint16_t)cycles;
            break;

        case 0x40u:
        case 0xC0u:
            crem = cycles;
            if (v->t1reload) {
                crem--;
                v->t1c      = (uint16_t)((v->t1l_h << 8) | v->t1l_l);
                v->t1reload = 0;
            }
            while (crem > v->t1c) {
                crem  -= (v->t1c + 1u);
                v->t1c = 0xFFFFu;
                via_set_irq(v, VIRQF_T1);
                if (v->acr & 0x80u) {
                    v->orb ^= 0x80u;
                    if (v->orbchange) v->orbchange(v);
                }
                if (!crem) {
                    v->t1reload = 1;
                    break;
                }
                crem--;
                v->t1c = (uint16_t)((v->t1l_h << 8) | v->t1l_l);
            }
            v->t1c -= (uint16_t)crem;
            break;
    }

    /* Timer 2 */
    if ((v->acr & ACRF_T2CON) == 0u) {
        crem = cycles;
        if (v->t2reload) {
            crem--;
            v->t2reload = 0;
        }
        if (v->t2run && crem > v->t2c) {
            via_set_irq(v, VIRQF_T2);
            v->t2run = false;
        }
        v->t2c -= (uint16_t)crem;
    }

    /* Shift register */
    switch (v->acr & ACRF_SRCON) {
        case 0x10u:
            if (!v->srtrigger) break;
            v->srtime -= (int16_t)cycles;
            while (v->srtime <= 0) {
                v->srtime += (int16_t)(v->t2l_l + 1u);
                v->cb1 ^= 1u;
                if (!v->cb1) {
                    v->cb2 = (v->sr & 0x80u) ? 1u : 0u;
                    v->sr  = (uint8_t)((v->sr << 1) | v->cb2);
                    if (v->cb2shifted) v->cb2shifted(v);
                    v->srcount = (uint8_t)((v->srcount + 1u) & 7u);
                }
            }
            break;

        case 0x14u:
            if (!v->srtrigger || v->srcount == 8u) break;
            v->srtime -= (int16_t)cycles;
            while (v->srtime <= 0) {
                v->srtime += (int16_t)(v->t2l_l + 1u);
                v->cb1 ^= 1u;
                if (!v->cb1) {
                    v->cb2 = (v->sr & 0x80u) ? 1u : 0u;
                    v->sr  = (uint8_t)((v->sr << 1) | v->cb2);
                    if (v->cb2shifted) v->cb2shifted(v);
                    v->srcount++;
                    if (v->srcount == 8u) {
                        via_set_irq(v, VIRQF_SR);
                        v->srtime    = 0;
                        v->srtrigger = false;
                        break;
                    }
                }
            }
            break;

        case 0x18u:
            if (!v->srtrigger || v->srcount == 8u) break;
            v->cb1 ^= 1u;
            if (!v->cb1) {
                v->cb2 = (v->sr & 0x80u) ? 1u : 0u;
                v->sr  = (uint8_t)((v->sr << 1) | v->cb2);
                if (v->cb2shifted) v->cb2shifted(v);
                v->srcount++;
                if (v->srcount == 8u) {
                    via_set_irq(v, VIRQF_SR);
                    v->srtrigger = false;
                }
            }
            break;

        default:
            if (v->ifr & VIRQF_SR) {
                via_clr_irq(v, VIRQF_SR);
            }
            v->srcount   = 0;
            v->srtrigger = false;
            break;
    }
}

void __not_in_flash_func(via_write)(struct via *v, int offset, uint8_t data)
{
    uint8_t tmp;
    offset &= 0x0F;

    switch (offset) {
        case VIA_IORB:
            tmp    = v->orb;
            v->orb = data;
            via_clr_irq(v, VIRQF_CB1);
            switch (v->pcr & PCRF_CB2CON) {
                case 0x00u:
                case 0x40u: via_clr_irq(v, VIRQF_CB2); break;
                case 0xA0u: v->cb2pulse = true; /* fallthrough */
                case 0x80u: v->cb2 = 0; break;
            }
            if (v->w_iorb)    v->w_iorb(v, tmp);
            if (v->orbchange) v->orbchange(v);
            break;

        case VIA_IORA:
            v->ora = data;
            via_clr_irq(v, VIRQF_CA1);
            switch (v->pcr & PCRF_CA2CON) {
                case 0x00u:
                case 0x04u: via_clr_irq(v, VIRQF_CA2); break;
                case 0x0Au: v->ca2pulse = true; /* fallthrough */
                case 0x08u: v->ca2 = 0; break;
            }
            if (v->w_iora) v->w_iora(v);
            break;

        case VIA_DDRB:
            v->ddrb = data;
            if (v->w_ddrb) v->w_ddrb(v);
            break;

        case VIA_DDRA:
            v->ddra = data;
            if (v->w_ddra) v->w_ddra(v);
            break;

        case VIA_T1C_H:
            v->t1l_h    = data;
            v->t1c      = (uint16_t)((v->t1l_h << 8) | v->t1l_l);
            v->t1reload = 1;
            via_clr_irq(v, VIRQF_T1);
            v->t1run = true;
            if ((v->acr & ACRF_T1CON) == 0x80u) {
                v->orb &= 0x7Fu;
                if (v->orbchange) v->orbchange(v);
            }
            break;

        case VIA_T1C_L:
        case VIA_T1L_L:
            v->t1l_l = data;
            break;

        case VIA_T1L_H:
            v->t1l_h = data;
            via_clr_irq(v, VIRQF_T1);
            break;

        case VIA_T2C_L:
            v->t2l_l = data;
            break;

        case VIA_T2C_H:
            v->t2l_h    = data;
            v->t2c      = (uint16_t)((v->t2l_h << 8) | v->t2l_l);
            v->t2reload = 1;
            via_clr_irq(v, VIRQF_T2);
            v->t2run = true;
            break;

        case VIA_SR:
            v->sr        = data;
            v->srcount   = 0;
            v->srtime    = 0;
            v->srtrigger = true;
            via_clr_irq(v, VIRQF_SR);
            break;

        case VIA_ACR:
            v->acr = data;
            if (((data & ACRF_T1CON) != 0x40u) && ((data & ACRF_T1CON) != 0xC0u)) {
                v->t1reload = 0;
            }
            break;

        case VIA_PCR:
            v->pcr = data;
            switch (v->pcr & PCRF_CA2CON) {
                case 0x0Cu: v->ca2 = 0; v->ca2pulse = false; break;
                case 0x0Eu: v->ca2 = 1; break;
            }
            switch (v->pcr & PCRF_CB2CON) {
                case 0xC0u: v->cb2 = 0; v->cb2pulse = false; break;
                case 0xE0u: v->cb2 = 1; break;
            }
            if (v->w_pcr) v->w_pcr(v);
            break;

        case VIA_IFR:
            if (data & VIRQF_CA1) v->iral = v->ira;
            if (data & VIRQF_CB1) v->irbl = v->irb;
            v->ifr &= (uint8_t)((~data) & 0x7Fu);
            if ((v->ier & v->ifr) & 0x7Fu) {
                v->ifr |= 0x80u;
            } else {
                v->cpuIrq &= (uint8_t)~v->irqbit;
            }
            break;

        case VIA_IER:
            if (data & 0x80u) v->ier |= (data & 0x7Fu);
            else              v->ier &= (uint8_t)~(data & 0x7Fu);
            via_check_irq(v);
            break;

        case VIA_IORA2:
            v->ora = data;
            if (v->w_iora2) v->w_iora2(v);
            break;
    }
}

uint8_t via_mon_read(const struct via *v, int offset)
{
    switch (offset & 0x0F) {
        case VIA_IORB:  return (uint8_t)((v->orb & v->ddrb) | (v->irbl & (uint8_t)~v->ddrb));
        case VIA_IORA:  return (uint8_t)((v->ora & v->ddra) | (v->iral & (uint8_t)~v->ddra));
        case VIA_DDRB:  return v->ddrb;
        case VIA_DDRA:  return v->ddra;
        case VIA_T1C_L: return (uint8_t)(v->t1c & 0xFFu);
        case VIA_T1C_H: return (uint8_t)(v->t1c >> 8);
        case VIA_T1L_L: return v->t1l_l;
        case VIA_T1L_H: return v->t1l_h;
        case VIA_T2C_L: return (uint8_t)(v->t2c & 0xFFu);
        case VIA_T2C_H: return (uint8_t)(v->t2c >> 8);
        case VIA_SR:    return v->sr;
        case VIA_ACR:   return v->acr;
        case VIA_PCR:   return v->pcr;
        case VIA_IFR:   return v->ifr;
        case VIA_IER:   return (uint8_t)(v->ier | 0x80u);
        case VIA_IORA2: return (uint8_t)((v->ora & v->ddra) | (v->iral & (uint8_t)~v->ddra));
    }
    return 0;
}

uint8_t __not_in_flash_func(via_read)(struct via *v, int offset)
{
    switch (offset & 0x0F) {
        case VIA_IORB:
            via_clr_irq(v, VIRQF_CB1);
            switch (v->pcr & PCRF_CB2CON) {
                case 0x00u:
                case 0x40u: via_clr_irq(v, VIRQF_CB2); break;
                case 0xA0u: v->cb2pulse = true; /* fallthrough */
                case 0x80u: v->cb2 = 0; break;
            }
            if (v->r_iorb) v->r_iorb(v);
            {
                uint8_t val = (uint8_t)((v->orb & v->ddrb) | (v->irbl & (uint8_t)~v->ddrb));
                return s_keyFound ? (uint8_t)(val | 0x08u) : (uint8_t)(val & 0xF7u);
            }

        case VIA_IORA:
            via_clr_irq(v, VIRQF_CA1);
            switch (v->pcr & PCRF_CA2CON) {
                case 0x00u:
                case 0x04u: via_clr_irq(v, VIRQF_CA2); break;
                case 0x0Au: v->ca2pulse = true; /* fallthrough */
                case 0x08u: v->ca2 = 0; break;
            }
            if (v->r_iora) v->r_iora(v);
            return (uint8_t)((v->ora & v->ddra) | (v->iral & (uint8_t)~v->ddra));

        case VIA_DDRB:  return v->ddrb;
        case VIA_DDRA:  return v->ddra;
        case VIA_T1C_L: via_clr_irq(v, VIRQF_T1); return (uint8_t)(v->t1c & 0xFFu);
        case VIA_T1C_H: return (uint8_t)(v->t1c >> 8);
        case VIA_T1L_L: return v->t1l_l;
        case VIA_T1L_H: return v->t1l_h;
        case VIA_T2C_L: via_clr_irq(v, VIRQF_T2); return (uint8_t)(v->t2c & 0xFFu);
        case VIA_T2C_H: return (uint8_t)(v->t2c >> 8);
        case VIA_SR:
            v->srcount   = 0;
            v->srtime    = 0;
            v->srtrigger = true;
            via_clr_irq(v, VIRQF_SR);
            return v->sr;
        case VIA_ACR:   return v->acr;
        case VIA_PCR:   return v->pcr;
        case VIA_IFR:   return v->ifr;
        case VIA_IER:   return (uint8_t)(v->ier | 0x80u);
        case VIA_IORA2: return (uint8_t)((v->ora & v->ddra) | (v->iral & (uint8_t)~v->ddra));
    }
    return 0;
}

void via_write_CA1(struct via *v, uint8_t data)
{
    if (!v->ca1 && data) {
        if (v->pcr & PCRF_CA1CON) via_set_irq(v, VIRQF_CA1);
    } else if (v->ca1 && !data) {
        if (!(v->pcr & PCRF_CA1CON)) via_set_irq(v, VIRQF_CA1);
    }
    v->ca1 = data ? 1u : 0u;
}

void via_write_CA2(struct via *v, uint8_t data)
{
    switch (v->pcr & PCRF_CA2CON) {
        case 0x00u:
        case 0x02u:
            if (v->ca2 && !data) via_set_irq(v, VIRQF_CA2);
            v->ca2 = data ? 1u : 0u;
            break;
        case 0x04u:
        case 0x06u:
            if (!v->ca2 && data) via_set_irq(v, VIRQF_CA2);
            v->ca2 = data ? 1u : 0u;
            break;
    }
    if (v->w_ca2ext) v->w_ca2ext(v);
}

void via_write_CB1(struct via *v, uint8_t data)
{
    if (!v->cb1 && data) {
        if (v->pcr & PCRF_CB1CON) via_set_irq(v, VIRQF_CB1);
    } else if (v->cb1 && !data) {
        if (!(v->pcr & PCRF_CB1CON)) via_set_irq(v, VIRQF_CB1);
    }
    v->cb1 = data ? 1u : 0u;
}

void via_write_CB2(struct via *v, uint8_t data)
{
    switch (v->pcr & PCRF_CB2CON) {
        case 0x00u:
        case 0x20u:
            if (v->cb2 && !data) via_set_irq(v, VIRQF_CB2);
            v->cb2 = data ? 1u : 0u;
            break;
        case 0x40u:
        case 0x60u:
            if (!v->cb2 && data) via_set_irq(v, VIRQF_CB2);
            v->cb2 = data ? 1u : 0u;
            break;
    }
    if (v->w_cb2ext) v->w_cb2ext(v);
}
