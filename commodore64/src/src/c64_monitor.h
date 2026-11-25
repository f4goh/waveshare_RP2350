#ifndef C64_MONITOR_H
#define C64_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void c64_monitor_init(void);
void c64_monitor_open(void);
void c64_monitor_close(void);
bool c64_monitor_is_open(void);
void c64_monitor_render(void);
void c64_monitor_key(uint8_t ascii);
void c64_monitor_uart_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* C64_MONITOR_H */
