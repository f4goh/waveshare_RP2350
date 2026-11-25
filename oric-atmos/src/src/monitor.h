/*
 * Mini-moniteur et debogueur integre Oric Atmos
 */

#ifndef ORIC_MONITOR_H
#define ORIC_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

void monitor_run(void);

bool monitor_save_snap(const char *base_name);
bool monitor_load_snap(const char *base_name);
bool monitor_save_bmp(const char *base_name);

bool monitor_save_snap_psram(uint8_t slot);
bool monitor_load_snap_psram(uint8_t slot);
bool monitor_psram_slot_valid(uint8_t slot);

#endif /* ORIC_MONITOR_H */
