#ifndef INPUT_SCUMM_H
#define INPUT_SCUMM_H

#include "machines/scumm/scumm_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

void input_scumm_init(void);
void input_scumm_poll(scumm_system *sys);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_SCUMM_H */
