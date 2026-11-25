#ifndef PSRAM_ALLOCATOR_H
#define PSRAM_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MURMDOOM_PSRAM_SIZE_BYTES
#define MURMDOOM_PSRAM_SIZE_BYTES (8u * 1024u * 1024u)
#endif

void *psram_malloc(size_t size);
void *psram_realloc(void *ptr, size_t size);
void psram_free(void *ptr);
void psram_reset(void);
void psram_mark_session(void);
void psram_restore_session(void);
void *psram_get_scratch_1(size_t size);
void *psram_get_scratch_2(size_t size);
void *psram_get_file_buffer(size_t size);

void psram_set_temp_mode(int enable);
void psram_reset_temp(void);
size_t psram_get_temp_offset(void);
void psram_set_temp_offset(size_t offset);

void psram_set_sram_mode(int enable);

#ifdef __cplusplus
}
#endif

#endif
