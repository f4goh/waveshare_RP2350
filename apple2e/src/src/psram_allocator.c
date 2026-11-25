#include "psram_allocator.h"
#include "board_hw.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PSRAM_BASE 0x11000000UL
#define PSRAM_SIZE ((size_t)MURMDOOM_PSRAM_SIZE_BYTES)

static uint8_t *psram_start = (uint8_t *)PSRAM_BASE;
#define SCRATCH_SIZE (512 * 1024)
static size_t psram_offset = SCRATCH_SIZE;

#define TEMP_SIZE (4 * 1024 * 1024)
#define PERM_SIZE (PSRAM_SIZE - TEMP_SIZE)
static size_t psram_temp_offset = 0;
static int psram_temp_mode = 0;
static int psram_sram_mode = 0;
static size_t psram_session_mark = 0;

void psram_set_temp_mode(int enable) {
    psram_temp_mode = enable;
}

void psram_set_sram_mode(int enable) {
    psram_sram_mode = enable;
}

void psram_reset_temp(void) {
    psram_temp_offset = 0;
}

size_t psram_get_temp_offset(void) {
    return psram_temp_offset;
}

void psram_set_temp_offset(size_t offset) {
    psram_temp_offset = offset;
}

void *psram_malloc(size_t size) {
    if (psram_sram_mode || board_psram_size() == 0) {
        return malloc(size);
    }

    size = (size + 3) & ~3;
    size_t total_size = size + sizeof(size_t);

    if (psram_temp_mode) {
        if (psram_temp_offset + total_size > TEMP_SIZE) {
            return malloc(size);
        }
        size_t *header = (size_t *)(psram_start + PERM_SIZE + psram_temp_offset);
        *header = size;
        void *ptr = (void *)(header + 1);
        psram_temp_offset += total_size;
        return ptr;
    } else {
        if (psram_offset + total_size > PERM_SIZE) {
            return malloc(size);
        }
        size_t *header = (size_t *)(psram_start + psram_offset);
        *header = size;
        void *ptr = (void *)(header + 1);
        psram_offset += total_size;
        return ptr;
    }
}

void *psram_realloc(void *ptr, size_t new_size) {
    if (ptr == NULL) return psram_malloc(new_size);
    if (new_size == 0) { psram_free(ptr); return NULL; }

    if ((uintptr_t)ptr >= PSRAM_BASE && (uintptr_t)ptr < (PSRAM_BASE + PSRAM_SIZE)) {
        size_t *header = (size_t *)ptr - 1;
        size_t old_size = *header;

        if (new_size <= old_size) {
            return ptr;
        }

        void *new_ptr = psram_malloc(new_size);
        if (new_ptr) {
            memcpy(new_ptr, ptr, old_size);
        }
        return new_ptr;
    }

    return realloc(ptr, new_size);
}

void *psram_get_scratch_1(size_t size) {
    if (size > 128 * 1024 || board_psram_size() == 0) return NULL;
    return psram_start;
}

void *psram_get_scratch_2(size_t size) {
    if (size > 128 * 1024 || board_psram_size() == 0) return NULL;
    return psram_start + (128 * 1024);
}

void *psram_get_file_buffer(size_t size) {
    if (size > 256 * 1024 || board_psram_size() == 0) return NULL;
    return psram_start + (256 * 1024);
}

void psram_free(void *ptr) {
    if ((uintptr_t)ptr >= PSRAM_BASE && (uintptr_t)ptr < (PSRAM_BASE + PSRAM_SIZE)) {
        return;
    }
    free(ptr);
}

void psram_reset(void) {
    psram_offset = SCRATCH_SIZE;
    psram_temp_offset = 0;
    psram_session_mark = 0;
}

void psram_mark_session(void) {
    psram_session_mark = psram_offset;
}

void psram_restore_session(void) {
    if (psram_session_mark == 0) return;
    psram_offset = psram_session_mark;
    psram_temp_offset = 0;
}
