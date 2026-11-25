/*
 * Emulation MOS 6522 VIA + interface AY-3-8912 + scan matrice clavier
 */

#ifndef VIA6522_H
#define VIA6522_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  ifr;
    uint8_t  irb, orb, irbl;
    uint8_t  ira, ora, iral;
    uint8_t  ddra;
    uint8_t  ddrb;
    uint8_t  t1l_l;
    uint8_t  t1l_h;
    uint16_t t1c;
    uint8_t  t2l_l;
    uint8_t  t2l_h;
    uint16_t t2c;
    uint8_t  sr;
    uint8_t  acr;
    uint8_t  pcr;
    uint8_t  ier;

    uint8_t  ca1, ca2;
    uint8_t  cb1, cb2;
    uint8_t  srcount;
    uint8_t  t1reload, t2reload;
    int16_t  srtime;

    bool     t1run, t2run;
    bool     ca2pulse, cb2pulse;
    bool     srtrigger;

    uint8_t  cpuIrq;
    uint8_t  irqbit;
} ViaSave;

struct via {
    /* Champs identiques a ViaSave pour sauvegarde explicite */
    uint8_t  ifr;
    uint8_t  irb, orb, irbl;
    uint8_t  ira, ora, iral;
    uint8_t  ddra;
    uint8_t  ddrb;
    uint8_t  t1l_l;
    uint8_t  t1l_h;
    uint16_t t1c;
    uint8_t  t2l_l;
    uint8_t  t2l_h;
    uint16_t t2c;
    uint8_t  sr;
    uint8_t  acr;
    uint8_t  pcr;
    uint8_t  ier;

    uint8_t  ca1, ca2;
    uint8_t  cb1, cb2;
    uint8_t  srcount;
    uint8_t  t1reload, t2reload;
    int16_t  srtime;

    bool     t1run, t2run;
    bool     ca2pulse, cb2pulse;
    bool     srtrigger;

    uint8_t  cpuIrq;
    uint8_t  irqbit;

    /* Callbacks internes */
    void    (*w_iorb)(struct via *, uint8_t);
    void    (*w_iora)(struct via *);
    void    (*w_iora2)(struct via *);
    void    (*w_ddra)(struct via *);
    void    (*w_ddrb)(struct via *);
    void    (*w_pcr)(struct via *);
    void    (*w_ca2ext)(struct via *);
    void    (*w_cb2ext)(struct via *);
    void    (*r_iora)(struct via *);
    void    (*r_iora2)(struct via *);
    void    (*r_iorb)(struct via *);
    void    (*ca2pulsed)(struct via *);
    void    (*cb2pulsed)(struct via *);
    void    (*cb2shifted)(struct via *);
    void    (*orbchange)(struct via *);

    uint8_t (*read_port_a)(struct via *);
    uint8_t (*read_port_b)(struct via *);
    void    (*write_port_a)(struct via *, uint8_t, uint8_t);
    void    (*write_port_b)(struct via *, uint8_t, uint8_t);
};

typedef struct {
    uint8_t bmode;
    uint8_t creg;
    uint8_t colonnes;
} Ay8912Bus;

#define AYBMF_BC1   (1u << 0)
#define AYBMF_BDIR  (1u << 1)

#define VIA_IORB    0
#define VIA_IORA    1
#define VIA_DDRB    2
#define VIA_DDRA    3
#define VIA_T1C_L   4
#define VIA_T1C_H   5
#define VIA_T1L_L   6
#define VIA_T1L_H   7
#define VIA_T2C_L   8
#define VIA_T2C_H   9
#define VIA_SR      10
#define VIA_ACR     11
#define VIA_PCR     12
#define VIA_IFR     13
#define VIA_IER     14
#define VIA_IORA2   15

#define PCRF_CA1CON (1u << 0)
#define PCRF_CA2CON 0x0Eu
#define PCRF_CB1CON (1u << 4)
#define PCRF_CB2CON 0xE0u

#define ACRF_PALATCH (1u << 0)
#define ACRF_PBLATCH (1u << 1)
#define ACRF_SRCON   0x1Cu
#define ACRF_T2CON   (1u << 5)
#define ACRF_T1CON   0xC0u

#define VIRQF_CA2 (1u << 0)
#define VIRQF_CA1 (1u << 1)
#define VIRQF_SR  (1u << 2)
#define VIRQF_CB2 (1u << 3)
#define VIRQF_CB1 (1u << 4)
#define VIRQF_T2  (1u << 5)
#define VIRQF_T1  (1u << 6)

void    via_init(struct via *v);
void    via_clock(struct via *v, unsigned int cycles);
void    via_write(struct via *v, int offset, uint8_t data);
uint8_t via_read(struct via *v, int offset);
uint8_t via_mon_read(const struct via *v, int offset);

void    via_write_CA1(struct via *v, uint8_t data);
void    via_write_CA2(struct via *v, uint8_t data);
void    via_write_CB1(struct via *v, uint8_t data);
void    via_write_CB2(struct via *v, uint8_t data);

uint8_t viaSize(void);
void    viaSaveState(const struct via *v, uint8_t *dst);
void    viaLoadState(struct via *v, const uint8_t *src);

#endif /* VIA6522_H */
