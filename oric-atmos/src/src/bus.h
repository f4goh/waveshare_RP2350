/*
 * Bus systeme Oric Atmos (CPU 6502, RAM/ROM, VIA 6522, ACIA 6551, boucle temps reel)
 */

#ifndef ORIC_BUS_H
#define ORIC_BUS_H

#include <stdbool.h>
#include <stdint.h>
#include "acia6551.h"
#include "via6522.h"
#include "vrEmu6502.h"

extern uint8_t     ram[];
extern uint8_t     overlayRam[];
extern uint8_t     ramMoni[];
extern VrEmu6502  *g_cpu;
extern struct via  g_via;
extern struct acia g_acia;

void    busInit(void);
void    busMainLoop(void);
void    busResetMachine(void);
void    busStepInstruction(void);
void    busAutoType(const char *str, uint16_t delay_frames);

uint8_t busRead(uint16_t addr, bool isDbg);
void    busWrite(uint16_t addr, uint8_t val);

#endif /* ORIC_BUS_H */
