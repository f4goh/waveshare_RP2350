#ifndef APPLE_CONFIG_MENU_H
#define APPLE_CONFIG_MENU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void apple_config_menu_open(void);
void apple_config_menu_close(void);
bool apple_config_menu_is_open(void);
void apple_config_menu_render(void);
void apple_config_menu_key(uint8_t key);

#ifdef __cplusplus
}
#endif

#endif /* APPLE_CONFIG_MENU_H */
