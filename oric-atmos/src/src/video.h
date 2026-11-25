/*
 * Interface video VGA + ULA
 */

#ifndef ORIC_VIDEO_H
#define ORIC_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

void    videoInit(void);
void    monitorEnable(bool enable);
void    videoSetMode(uint8_t hires);
uint8_t videoGetPattr(void);
void    videoSetPalette(uint8_t palette);
uint8_t videoGetPalette(void);

#endif /* ORIC_VIDEO_H */
