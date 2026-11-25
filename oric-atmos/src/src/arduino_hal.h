/*
 * Pont minimal C <-> Arduino (Earle Philhower) pour SD et Wire (I2C)
 */

#ifndef ARDUINO_HAL_H
#define ARDUINO_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- SD Card ---- */
bool hal_sd_init(void);
bool hal_sd_is_ready(void);

/* Liste les fichiers ayant l'extension `ext` (ex: ".tap") sans l'extension dans `names`.
 * Retourne le nombre de fichiers trouves (<= max_files), ou -1 en cas d'erreur SD. */
int hal_sd_list_files(const char *ext,
                      char names[][SD_MAX_FILENAME_LEN],
                      size_t max_files,
                      bool strip_ext);

/* Lecture / ecriture complete de fichiers */
int hal_sd_read_file(const char *filename, uint8_t *buf, size_t max_size, size_t *bytes_read);
int hal_sd_write_file(const char *filename, const uint8_t *buf, size_t size);

/* Snapshots (entete + RAM 48 Ko) */
int hal_sd_save_snap(const char *filename,
                     const uint8_t *hdr, size_t hdr_size,
                     const uint8_t *ram, size_t ram_size);
int hal_sd_load_snap(const char *filename,
                     uint8_t *hdr, size_t hdr_size,
                     uint8_t *ram, size_t ram_size);

/* Ecriture flux (ex: BMP sans copier tout l'ecran en RAM) et lecture flux (.DSK) */
typedef void *HalFileHandle;
HalFileHandle hal_file_open_write(const char *filename);
bool hal_file_write(HalFileHandle h, const uint8_t *data, size_t len);
void hal_file_close(HalFileHandle h);

HalFileHandle hal_file_open_read(const char *filename, uint32_t *size_out);
size_t hal_file_read(HalFileHandle h, uint8_t *data, size_t len);
void hal_file_close_read(HalFileHandle h);

/* Flux lecture TAP avec cache secteur 512 octets (economise 64 Ko de RAM) */
bool hal_tape_open(const char *filename);
void hal_tape_close(void);
bool hal_tape_is_open(void);
uint32_t hal_tape_size(void);
uint32_t hal_tape_tell(void);
bool hal_tape_seek(uint32_t pos);
bool hal_tape_read_byte(uint8_t *out);

/* ---- Bus I2C (Wire) ---- */
void hal_i2c_init(void);
bool hal_i2c_probe(uint8_t addr);
bool hal_i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *buf, uint8_t len);
int  hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len);

/* ---- LED ---- */
void hal_led_set(bool on);

#ifdef __cplusplus
}
#endif

#endif /* ARDUINO_HAL_H */
