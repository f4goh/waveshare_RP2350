/*
 * hal_waveshare.cpp - Backend ArcadeHAL pour Waveshare RP2350-PiZero
 *   - Video   : 320x240 RGB565 -> HDMI/DVI 640x480p60 (Core 1, PIO0)
 *   - Memoire : 8 Mo PSRAM QSPI (GPIO 47) pour l'arene 2 Mo SCUMM
 *   - Stockage: MicroSD SPI1 (FatFS)
 *   - Audio   : 22050 Hz -> reechantillonnage 48000 Hz vers Audio 3-en-1 (HDMI/I2S/PWM)
 */

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

#include "hal/arcade_hal_video.h"
#include "hal/arcade_hal_memory.h"
#include "hal/arcade_hal_storage.h"
#include "hal/arcade_hal_audio.h"
#include "hal/arcade_hal_input.h"

#include "board_hw.h"
#include "psram_allocator.h"
#include "hdmi_dvi.h"
#include "audio.h"

extern "C" {
#include "ff.h"
}

/* ==========================================================================
 * 1. VIDEO (320x240 RGB565 -> HDMI)
 * ========================================================================== */
const uint32_t HAL_VIDEO_WIDTH  = 320;
const uint32_t HAL_VIDEO_HEIGHT = 240;

uint16_t g_scumm_fb[320 * 240];
static uint32_t s_scan_y = 0;

bool hal_video_init(void)
{
    memset(g_scumm_fb, 0, sizeof(g_scumm_fb));
    s_scan_y = 0;
    return true;
}

uint16_t *hal_video_acquire_scanline(void)
{
    if (s_scan_y >= HAL_VIDEO_HEIGHT) {
        s_scan_y = 0;
    }
    return &g_scumm_fb[s_scan_y * HAL_VIDEO_WIDTH];
}

void hal_video_submit_scanline(uint16_t *buf)
{
    (void)buf;
    s_scan_y++;
    if (s_scan_y >= HAL_VIDEO_HEIGHT) {
        s_scan_y = 0;
    }
}

void hal_video_run(void)
{
    /* Core 1 est gere par hdmi_dvi_launch_core1() */
    for (;;) {
        tight_loop_contents();
    }
}

void hal_video_probe_readback(void) {}
uint32_t hal_video_take_blocked_us(void) { return 0; }
uint32_t hal_video_near_empty_count(void) { return 0; }

/* ==========================================================================
 * 2. MEMOIRE (8 Mo PSRAM APS6404L)
 * ========================================================================== */
void *hal_mem_bulk_alloc(size_t size)
{
    return psram_malloc(size);
}

size_t hal_mem_bulk_free(void)
{
    return (size_t)(MURMDOOM_PSRAM_SIZE_BYTES / 2u);
}

void *hal_mem_fast_alloc(size_t size)
{
    void *p = malloc(size);
    return p ? p : psram_malloc(size);
}

/* ==========================================================================
 * 3. STOCKAGE (MicroSD SPI1 via FatFS)
 * ========================================================================== */
struct hal_file {
    FIL      fil;
    bool     in_use;
};

#define HAL_MAX_FILES 8
static struct hal_file s_halFiles[HAL_MAX_FILES];
static FATFS           s_fatfs;
static bool            s_sdMounted = false;

static struct hal_file *alloc_hal_file(void)
{
    for (int i = 0; i < HAL_MAX_FILES; i++) {
        if (!s_halFiles[i].in_use) {
            memset(&s_halFiles[i], 0, sizeof(s_halFiles[i]));
            s_halFiles[i].in_use = true;
            return &s_halFiles[i];
        }
    }
    return nullptr;
}

static const char *norm_path(const char *path)
{
    if (!path) return "";
    /* FatFS racine = "" ou "/..." */
    return path;
}

bool hal_storage_mount(void)
{
    if (s_sdMounted) return true;
    FRESULT fr = f_mount(&s_fatfs, "", 1);
    if (fr == FR_OK) {
        s_sdMounted = true;
        return true;
    }
    return false;
}

void hal_storage_unmount(void)
{
    if (s_sdMounted) {
        f_mount(nullptr, "", 0);
        s_sdMounted = false;
    }
}

bool hal_storage_list_dir(const char *dir, hal_storage_dirent_cb cb, void *ctx)
{
    if (!s_sdMounted && !hal_storage_mount()) return false;
    DIR dp;
    FILINFO fno;
    FRESULT fr = f_opendir(&dp, norm_path(dir));
    if (fr != FR_OK) return false;
    for (;;) {
        fr = f_readdir(&dp, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;
        if (!(fno.fattrib & AM_DIR)) {
            cb(fno.fname, ctx);
        }
    }
    f_closedir(&dp);
    return true;
}

hal_file_t *hal_storage_open(const char *path)
{
    if (!s_sdMounted && !hal_storage_mount()) return nullptr;
    struct hal_file *hf = alloc_hal_file();
    if (!hf) return nullptr;
    if (f_open(&hf->fil, norm_path(path), FA_READ) != FR_OK) {
        hf->in_use = false;
        return nullptr;
    }
    return hf;
}

uint32_t hal_storage_read(hal_file_t *f, void *buf, uint32_t len)
{
    if (!f || !f->in_use || !buf || len == 0) return 0;
    UINT br = 0;
    if (f_read(&f->fil, buf, len, &br) != FR_OK) return 0;
    return (uint32_t)br;
}

bool hal_storage_seek(hal_file_t *f, uint32_t pos)
{
    if (!f || !f->in_use) return false;
    return f_lseek(&f->fil, (FSIZE_t)pos) == FR_OK;
}

uint32_t hal_storage_size(hal_file_t *f)
{
    if (!f || !f->in_use) return 0;
    return (uint32_t)f_size(&f->fil);
}

void hal_storage_close(hal_file_t *f)
{
    if (!f || !f->in_use) return;
    f_close(&f->fil);
    f->in_use = false;
}

hal_file_t *hal_storage_create(const char *path)
{
    if (!s_sdMounted && !hal_storage_mount()) return nullptr;
    struct hal_file *hf = alloc_hal_file();
    if (!hf) return nullptr;
    if (f_open(&hf->fil, norm_path(path), FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        hf->in_use = false;
        return nullptr;
    }
    return hf;
}

uint32_t hal_storage_write(hal_file_t *f, const void *buf, uint32_t len)
{
    if (!f || !f->in_use || !buf || len == 0) return 0;
    UINT bw = 0;
    if (f_write(&f->fil, buf, len, &bw) != FR_OK) return 0;
    return (uint32_t)bw;
}

bool hal_storage_remove(const char *path)
{
    if (!s_sdMounted && !hal_storage_mount()) return false;
    FRESULT fr = f_unlink(norm_path(path));
    return (fr == FR_OK || fr == FR_NO_FILE);
}

bool hal_storage_rename(const char *from, const char *to)
{
    if (!s_sdMounted && !hal_storage_mount()) return false;
    return f_rename(norm_path(from), norm_path(to)) == FR_OK;
}

/* ==========================================================================
 * 4. AUDIO (22050 Hz SCUMM -> 48000 Hz HDMI / I2S / PWM)
 * ========================================================================== */
static hal_audio_fill_cb s_fillCb = nullptr;
static uint32_t          s_srcRate = 22050u;
static int32_t           s_srcBuf[400];
static int16_t           s_prevL = 0, s_prevR = 0;

bool hal_audio_init(uint32_t sample_rate)
{
    s_srcRate = (sample_rate > 0) ? sample_rate : 22050u;
    s_prevL = s_prevR = 0;
    return true;
}

void hal_audio_set_fill_callback(hal_audio_fill_cb cb)
{
    s_fillCb = cb;
}

uint32_t hal_audio_enter_critical(void)
{
    noInterrupts();
    return 0;
}

void hal_audio_exit_critical(uint32_t saved_state)
{
    (void)saved_state;
    interrupts();
}

void scumm_audio_pump_frame(void)
{
    /* 60 Hz frame -> 800 ech @ 48000 Hz depuis s_srcRate (22050 Hz -> 367/368 ech exacts) */
    static uint32_t s_rateRem = 0;
    s_rateRem += s_srcRate;
    const int src_n = (int)(s_rateRem / 60u);
    s_rateRem %= 60u;
    if (!s_fillCb || src_n <= 0 || src_n > 400) {
        for (int i = 0; i < 800; i++) {
            audioPushSidSample(0, 0);
        }
        return;
    }

    s_fillCb(s_srcBuf, src_n);

    /* Reechantillonnage lineaire src_n -> 800 ech 48 kHz */
    for (int i = 0; i < 800; i++) {
        uint32_t fp   = (uint32_t)((((uint64_t)i * (uint64_t)src_n) << 16) / 800ULL);
        int      idx  = (int)(fp >> 16);
        uint32_t frac = fp & 0xFFFFu;

        int32_t w0 = (idx == 0) ? (((int32_t)s_prevL << 16) | (uint16_t)s_prevR)
                                : s_srcBuf[idx - 1];
        int32_t w1 = s_srcBuf[(idx < src_n) ? idx : (src_n - 1)];

        int16_t l0 = (int16_t)(w0 >> 16), r0 = (int16_t)(w0 & 0xFFFF);
        int16_t l1 = (int16_t)(w1 >> 16), r1 = (int16_t)(w1 & 0xFFFF);

        int16_t l = (int16_t)(l0 + (((int32_t)(l1 - l0) * (int32_t)frac) >> 16));
        int16_t r = (int16_t)(r0 + (((int32_t)(r1 - r0) * (int32_t)frac) >> 16));
        audioPushSidSample(l, r);
    }

    int32_t last = s_srcBuf[src_n - 1];
    s_prevL = (int16_t)(last >> 16);
    s_prevR = (int16_t)(last & 0xFFFF);
}

/* ==========================================================================
 * 5. INPUT STUBS (geres par input_scumm.cpp)
 * ========================================================================== */
void hal_input_init(void) {}
uint32_t hal_input_read(void) { return 0; }
bool hal_input_connected(void) { return false; }
uint32_t hal_input_read_port(uint8_t port) { (void)port; return 0; }
bool hal_input_connected_port(uint8_t port) { (void)port; return false; }
