#ifndef SCUMM_OSD_H
#define SCUMM_OSD_H

#include <stdbool.h>
#include <stdint.h>
#include "machines/scumm/scumm_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

bool scumm_osd_is_open(void);
void scumm_osd_open(void);
void scumm_osd_close(void);
void scumm_osd_key(uint8_t key, scumm_system *sys);
void scumm_osd_render(uint16_t *fb320x240);
void scumm_osd_error_screen(uint16_t *fb320x240, uint16_t bg565, const char *title, const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif

#endif /* SCUMM_OSD_H */
