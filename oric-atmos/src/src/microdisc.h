/*
 * Emulation du controleur disquette Oric Microdisc (WD1793 + images .DSK MFM_DISK)
 * Adapte d'Oricutron pour RP2350-PiZero (stockage des images .DSK en PSRAM)
 */

#ifndef ORIC_MICRODISC_H
#define ORIC_MICRODISC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MICRODISC_MAX_DRIVES   2U
#define MICRODISC_TRACK_SIZE   6400U
#define MICRODISC_MAX_TRACKS   128U
#define MICRODISC_HEADER_SIZE  256U
#define MICRODISC_RAW_LEN      (MICRODISC_HEADER_SIZE + 2U * MICRODISC_MAX_TRACKS * MICRODISC_TRACK_SIZE)

#define MICRODISC_ROM_SIZE     8192U
#define MICRODISC_OVERLAY_SIZE 16384U

#define MICRODISC_NAME_LEN     64U

/* Adresses PSRAM pour les lecteurs A: et B: (apres la zone snapshots 0x100000..0x13FFFF) */
#define MICRODISC_PSRAM_OFF_DRIVE0 0x200000UL
#define MICRODISC_PSRAM_OFF_DRIVE1 0x400000UL

void        microdisc_init(void);
void        microdisc_reset(void);

void        microdisc_set_enabled(bool enabled);
bool        microdisc_is_enabled(void);

bool        microdisc_romdis(void);
bool        microdisc_diskrom(void);
bool        microdisc_irq_pending(void);

uint8_t     microdisc_read(uint16_t addr);
void        microdisc_write(uint16_t addr, uint8_t data, uint16_t cpu_op_pc);
void        microdisc_tick(uint8_t cycles);

/* Auto-sauvegarde differee sur SD apres inactivite d'ecriture */
bool        microdisc_should_autoflush(void);
void        microdisc_autoflush(void);

/* Gestion des disquettes .DSK (lecture/ecriture) */
bool        microdisc_insert_disk(uint8_t drive, const char *filename);
bool        microdisc_insert_image_mem(uint8_t drive, const char *name, const uint8_t *data, uint32_t len);
bool        microdisc_save_disk(uint8_t drive);
void        microdisc_eject_disk(uint8_t drive, bool save_if_modified);

bool        microdisc_has_disk(uint8_t drive);
bool        microdisc_is_modified(uint8_t drive);
const char *microdisc_disk_name(uint8_t drive);

/* Etat pour affichage Moniteur / OSD */
uint8_t     microdisc_current_drive(void);
uint8_t     microdisc_current_track(void);
uint8_t     microdisc_status_reg(void);
uint8_t     microdisc_wd_status_reg(void);
bool        microdisc_storage_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* ORIC_MICRODISC_H */
