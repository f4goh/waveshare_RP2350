#ifndef C64_CONFIG_MENU_H
#define C64_CONFIG_MENU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void c64_config_menu_open(void);
void c64_config_menu_close(void);
bool c64_config_menu_is_open(void);
void c64_config_menu_render(void);
void c64_config_menu_key(uint8_t key);

#ifdef __cplusplus
}
#endif

#endif /* C64_CONFIG_MENU_H */
