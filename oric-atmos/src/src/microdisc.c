/*
 * Emulation du controleur disquette Oric Microdisc (WD1793 + format .DSK MFM_DISK)
 * Porte depuis Oricutron pour RP2350-PiZero :
 *   - Stockage des images disquettes brutes (1.64 Mo max / lecteur) en PSRAM uncached
 *   - Lecture / ecriture secteur simple/multiple, lecture/ecriture piste (Formatage), CRC CCITT
 *   - Correctif MICRODISC_FUDGE ($E3A1) pour afficher "Insert system disc" sans lecteur pret
 */

#include "microdisc.h"
#include "arduino_hal.h"

#include <string.h>

#ifdef PICO_RP2350
#include "board_hw.h"
#include "pico.h"
#define MICRODISC_PSRAM_UNCACHED 0x15000000UL
#else
#ifndef __not_in_flash_func
#define __not_in_flash_func(fn) fn
#endif
#endif

/* --------------------------------------------------------------------------
 * Registres et constantes WD1793 / Microdisc (identiques Oricutron)
 * -------------------------------------------------------------------------- */
#define MDSF_BS0    0x01u
#define MDSF_BS1    0x02u
#define MDSF_BS     0x03u
#define MDSF_INTRQ  0x80u
#define MDSF_INTENA 0x01u
#define MDSF_ROMDIS 0x02u
#define MDSF_DSEL   0x04u
#define MDSF_DDEN   0x08u
#define MDSF_SIDE   0x10u
#define MDSF_DRIVE  0x60u
#define MDSF_EPROM  0x80u
#define MF_DRQ      0x80u

#define WSF_NOTREADY 0x80u
#define WSF_RNF      0x10u
#define WSF_CRCE     0x08u
#define WSF_LOSTDAT  0x04u
#define WSF_DRQ      0x02u
#define WSF_BUSY     0x01u

#define WSFI_WRPROT  0x40u
#define WSFI_HEADL   0x20u
#define WSFI_SEEKERR 0x10u
#define WSFI_TRK0    0x04u
#define WSFI_PULSE   0x02u

#define WSFR_RECTYP  0x20u

#define MICRODISC_MAX_SECTORS_TRACK 32U
#define MICRODISC_AUTOFLUSH_CYCLES  1500000UL /* ~1.5 s d'inactivite apres ecriture */

typedef enum {
    COP_NUFFINK = 0,
    COP_READ_SECTOR,
    COP_READ_SECTORS,
    COP_WRITE_SECTOR,
    COP_WRITE_SECTORS,
    COP_READ_TRACK,
    COP_WRITE_TRACK,
    COP_READ_ADDRESS,
    COP_READ_SECTOR_FUDGE,
    COP_READ_SECTORS_FUDGE
} WdOp;

typedef struct {
    uint8_t *id_ptr;
    uint8_t *data_ptr;
} MfmSector;

typedef struct {
    bool      mounted;
    bool      modified;
    bool      autoflush_tried;
    uint8_t   numsides;
    uint8_t   numtracks;
    uint32_t  numsides_in;
    uint32_t  numtracks_in;
    uint32_t  numsides_out;
    uint32_t  numtracks_out;
    uint32_t  geometry;
    int16_t   cachedtrack;
    int16_t   cachedside;
    uint16_t  numsectors;
    MfmSector sector[MICRODISC_MAX_SECTORS_TRACK];
    uint8_t  *rawimage;
    char      filename[MICRODISC_NAME_LEN];
} DiskImage;

typedef struct {
    uint8_t    r_status;
    uint8_t    r_track;
    uint8_t    r_sector;
    uint8_t    r_data;
    uint8_t    c_drive;
    uint8_t    c_side;
    uint8_t    c_track;
    uint8_t    c_sector;
    bool       last_step_in;
    WdOp       currentop;
    MfmSector *currsector;
    uint16_t   currseclen;
    uint16_t   curroffs;
    uint16_t   crc;
    uint8_t    sectype;
    int32_t    delayedint;
    int32_t    delayeddrq;
    int16_t    distatus;
    int16_t    ddstatus;
} Wd1793State;

typedef struct {
    bool    enabled;
    uint8_t status;
    uint8_t intrq;
    uint8_t drq;
    bool    romdis;
    bool    diskrom;
    bool    irq_line;
} MicrodiscCtrl;

static DiskImage     s_disks[MICRODISC_MAX_DRIVES];
static Wd1793State   s_wd;
static MicrodiscCtrl s_md;
static uint32_t      s_dirtyIdleCycles = 0u;
static uint8_t       s_ioBounce[4096];

#ifndef PICO_RP2350
static uint8_t s_hostDiskMem[MICRODISC_MAX_DRIVES][MICRODISC_RAW_LEN];
#endif

static uint8_t *get_drive_raw_buffer(uint8_t drive)
{
    if (drive >= MICRODISC_MAX_DRIVES) {
        return NULL;
    }
#ifdef PICO_RP2350
    uint32_t off = (drive == 0u) ? MICRODISC_PSRAM_OFF_DRIVE0 : MICRODISC_PSRAM_OFF_DRIVE1;
    if (board_psram_size() < (off + MICRODISC_RAW_LEN)) {
        return NULL;
    }
    return (uint8_t *)(MICRODISC_PSRAM_UNCACHED + off);
#else
    return s_hostDiskMem[drive];
#endif
}

static void raw_copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t len)
{
    for (uint32_t i = 0u; i < len; i++) {
        dst[i] = src[i];
    }
}

static void raw_move_backward(uint8_t *dst, const uint8_t *src, uint32_t len)
{
    for (uint32_t i = len; i > 0u; i--) {
        dst[i - 1u] = src[i - 1u];
    }
}

static void raw_zero_bytes(uint8_t *dst, uint32_t len)
{
    for (uint32_t i = 0u; i < len; i++) {
        dst[i] = 0u;
    }
}

static uint32_t raw_get_u32(const uint8_t *buf, uint32_t offset)
{
    return ((uint32_t)buf[offset]) |
           ((uint32_t)buf[offset + 1u] << 8) |
           ((uint32_t)buf[offset + 2u] << 16) |
           ((uint32_t)buf[offset + 3u] << 24);
}

static void raw_set_u32(uint8_t *buf, uint32_t offset, uint32_t val)
{
    buf[offset]      = (uint8_t)(val & 0xFFu);
    buf[offset + 1u] = (uint8_t)((val >> 8) & 0xFFu);
    buf[offset + 2u] = (uint8_t)((val >> 16) & 0xFFu);
    buf[offset + 3u] = (uint8_t)((val >> 24) & 0xFFu);
}

static uint16_t __not_in_flash_func(calc_crc)(uint16_t crc, uint8_t value)
{
    crc  = (uint16_t)(((uint8_t)(crc >> 8)) | (crc << 8));
    crc ^= value;
    crc ^= (uint16_t)(((uint8_t)(crc & 0xFFu)) >> 4);
    crc ^= (uint16_t)((crc << 8) << 4);
    crc ^= (uint16_t)(((crc & 0xFFu) << 4) << 1);
    return crc;
}

static void disk_set_modified(DiskImage *dimg, uint8_t side, uint8_t track)
{
    if (!dimg || !dimg->mounted) {
        return;
    }
    dimg->modified        = true;
    dimg->autoflush_tried = false;
    s_dirtyIdleCycles     = 0u;

    uint32_t s1 = (uint32_t)side + 1u;
    uint32_t t1 = (uint32_t)track + 1u;
    if (dimg->numsides_out < s1) {
        dimg->numsides_out = s1;
    }
    if (dimg->numtracks_out < t1) {
        dimg->numtracks_out = t1;
    }
}

static DiskImage *active_disk(void)
{
    if (s_wd.c_drive >= MICRODISC_MAX_DRIVES) {
        return NULL;
    }
    DiskImage *d = &s_disks[s_wd.c_drive];
    return (d->mounted && d->rawimage) ? d : NULL;
}

static uint8_t *track_ptr(DiskImage *dimg, uint8_t side, uint8_t track)
{
    if (!dimg || !dimg->rawimage || side >= dimg->numsides || track >= dimg->numtracks) {
        return NULL;
    }
    uint32_t off = ((uint32_t)side * dimg->numtracks + track) * MICRODISC_TRACK_SIZE + MICRODISC_HEADER_SIZE;
    if (off + MICRODISC_TRACK_SIZE > MICRODISC_RAW_LEN) {
        return NULL;
    }
    return &dimg->rawimage[off];
}

static void diskimage_cachetrack(DiskImage *dimg, int track, int side)
{
    if (!dimg || !dimg->mounted || !dimg->rawimage) {
        return;
    }
    if (dimg->cachedtrack == track && dimg->cachedside == side) {
        return;
    }

    for (uint32_t n = 0u; n < MICRODISC_MAX_SECTORS_TRACK; n++) {
        dimg->sector[n].id_ptr   = NULL;
        dimg->sector[n].data_ptr = NULL;
    }
    dimg->numsectors  = 0u;
    dimg->cachedtrack = -1;
    dimg->cachedside  = -1;

    if (side < 0 || track < 0 || side >= dimg->numsides || track >= dimg->numtracks) {
        return;
    }

    uint8_t *ptr = track_ptr(dimg, (uint8_t)side, (uint8_t)track);
    if (!ptr) {
        return;
    }
    uint8_t *eot = ptr + MICRODISC_TRACK_SIZE;
    uint16_t sectorcount = 0u;

    while (ptr < eot && sectorcount < MICRODISC_MAX_SECTORS_TRACK) {
        while ((ptr + 3) < eot &&
               !(ptr[0] == 0xA1u && ptr[1] == 0xA1u && ptr[2] == 0xA1u && ptr[3] == 0xFEu)) {
            ptr++;
        }
        ptr += 3;
        if (ptr + 7 >= eot) {
            break;
        }

        dimg->sector[sectorcount].id_ptr   = ptr;
        dimg->sector[sectorcount].data_ptr = NULL;
        sectorcount++;

        uint8_t n = ptr[4];
        ptr += 7;

        while (ptr < eot && ptr[0] != 0xFBu && ptr[0] != 0xF8u) {
            ptr++;
        }
        if (ptr >= eot) {
            break;
        }

        dimg->sector[sectorcount - 1u].data_ptr = ptr;
        uint32_t secbytes = (n <= 7u) ? (1UL << (n + 7u)) : 6400UL;
        ptr += secbytes + 3u;
    }

    if (sectorcount > 0u) {
        dimg->numsectors  = sectorcount;
        dimg->cachedtrack = (int16_t)track;
        dimg->cachedside  = (int16_t)side;
    }
}

/* --------------------------------------------------------------------------
 * Signaux IRQ / DRQ Microdisc
 * -------------------------------------------------------------------------- */
static void __not_in_flash_func(md_set_drq)(void)
{
    s_md.drq = 0u;
}

static void __not_in_flash_func(md_clr_drq)(void)
{
    s_md.drq = MF_DRQ;
}

static void __not_in_flash_func(md_set_intrq)(void)
{
    s_md.intrq = 0u;
    if ((s_md.status & MDSF_INTENA) != 0u) {
        s_md.irq_line = true;
    }
}

static void __not_in_flash_func(md_clr_intrq)(void)
{
    s_md.intrq    = MDSF_INTRQ;
    s_md.irq_line = false;
}

/* --------------------------------------------------------------------------
 * Controleur WD1793
 * -------------------------------------------------------------------------- */
static void wd_init(void)
{
    memset(&s_wd, 0, sizeof(s_wd));
    s_wd.distatus = -1;
    s_wd.ddstatus = -1;
}

static void wd_seek_track(uint8_t track, bool dofudge)
{
    DiskImage *dimg = active_disk();
    if (dimg) {
        if (track >= dimg->numtracks) {
            track = (dimg->numtracks > 0u) ? (uint8_t)(dimg->numtracks - 1u) : 0u;
            s_wd.distatus = (int16_t)(WSFI_HEADL | WSFI_SEEKERR);
        } else {
            s_wd.distatus = (int16_t)(WSFI_HEADL | WSFI_PULSE);
        }

        diskimage_cachetrack(dimg, track, s_wd.c_side);

        s_wd.c_track  = track;
        s_wd.c_sector = 0u;
        s_wd.r_track  = track;
        s_wd.delayedint = 20;
        if (s_wd.c_track == 0u) {
            s_wd.distatus |= WSFI_TRK0;
        }
        return;
    }

    md_set_intrq();
    s_wd.r_track = 0u;

    /* Workaround Euphoric/Oricutron : sans disquette au boot, laisse
     * MICRODIS.ROM sortir proprement vers "Insert system disc" */
    if (dofudge) {
        s_wd.r_status = 0x44u;
        return;
    }

    s_wd.r_status = (uint8_t)(WSF_NOTREADY | WSFI_SEEKERR);
}

static MfmSector *wd_find_sector(uint8_t secid)
{
    DiskImage *dimg = active_disk();
    if (!dimg) {
        return NULL;
    }

    diskimage_cachetrack(dimg, s_wd.c_track, s_wd.c_side);
    if (dimg->numsectors < 1u) {
        return NULL;
    }

    int revs = 0;
    while (revs < 2) {
        s_wd.c_sector = (uint8_t)((s_wd.c_sector + 1u) % dimg->numsectors);
        if (s_wd.c_sector == 0u) {
            revs++;
            s_wd.r_status |= WSFI_PULSE;
        }
        if (dimg->sector[s_wd.c_sector].id_ptr &&
            dimg->sector[s_wd.c_sector].id_ptr[3] == secid &&
            dimg->sector[s_wd.c_sector].data_ptr != NULL) {
            return &dimg->sector[s_wd.c_sector];
        }
    }
    return NULL;
}

static MfmSector *wd_first_sector(void)
{
    DiskImage *dimg = active_disk();
    if (!dimg) {
        return NULL;
    }
    diskimage_cachetrack(dimg, s_wd.c_track, s_wd.c_side);
    if (dimg->numsectors < 1u) {
        return NULL;
    }
    s_wd.c_sector = 0u;
    s_wd.r_status = WSFI_PULSE;
    return dimg->sector[0].id_ptr ? &dimg->sector[0] : NULL;
}

static MfmSector *wd_next_sector(void)
{
    DiskImage *dimg = active_disk();
    if (!dimg) {
        return NULL;
    }
    diskimage_cachetrack(dimg, s_wd.c_track, s_wd.c_side);
    if (dimg->numsectors < 1u) {
        return NULL;
    }
    s_wd.c_sector = (uint8_t)((s_wd.c_sector + 1u) % dimg->numsectors);
    if (s_wd.c_sector == 0u) {
        s_wd.r_status |= WSFI_PULSE;
    }
    return dimg->sector[s_wd.c_sector].id_ptr ? &dimg->sector[s_wd.c_sector] : NULL;
}

static bool sector_data_in_bounds(DiskImage *dimg, const MfmSector *sec, uint16_t seclen)
{
    if (!dimg || !sec || !sec->data_ptr) {
        return false;
    }
    uint8_t *trk = track_ptr(dimg, s_wd.c_side, s_wd.c_track);
    if (!trk) {
        return false;
    }
    ptrdiff_t off = sec->data_ptr - trk;
    if (off < 0) {
        return false;
    }
    /* ID octet (0xFB/0xF8) + data[seclen] + 2 octets CRC */
    return ((uint32_t)off + 1u + (uint32_t)seclen + 2u) <= MICRODISC_TRACK_SIZE;
}

static void start_read_sector(uint8_t cmd_byte, bool allow_fudge)
{
    if (allow_fudge && !active_disk()) {
        s_wd.currentop = (cmd_byte & 0x10u) ? COP_READ_SECTORS_FUDGE : COP_READ_SECTOR_FUDGE;
        return;
    }

    s_wd.curroffs   = 0u;
    s_wd.currsector = wd_find_sector(s_wd.r_sector);
    DiskImage *dimg = active_disk();
    if (!s_wd.currsector || !dimg) {
        s_wd.r_status  = WSF_RNF;
        md_clr_drq();
        md_set_intrq();
        s_wd.currentop = COP_NUFFINK;
        return;
    }

    uint8_t n = s_wd.currsector->id_ptr[4];
    s_wd.currseclen = (n <= 5u) ? (uint16_t)(1u << (n + 7u)) : 256u;
    if (!sector_data_in_bounds(dimg, s_wd.currsector, s_wd.currseclen)) {
        s_wd.r_status  = WSF_RNF;
        md_clr_drq();
        md_set_intrq();
        s_wd.currentop = COP_NUFFINK;
        return;
    }

    s_wd.r_status   = (uint8_t)(WSF_BUSY | WSF_NOTREADY);
    s_wd.delayeddrq = 60;
    s_wd.currentop  = (cmd_byte & 0x10u) ? COP_READ_SECTORS : COP_READ_SECTOR;
    s_wd.crc        = 0xE295u;
}

static uint8_t __not_in_flash_func(wd_read_reg)(uint16_t reg)
{
    switch (reg) {
        case 0u:
            md_clr_intrq();
            return s_wd.r_status;

        case 1u:
            return s_wd.r_track;

        case 2u:
            return s_wd.r_sector;

        case 3u: {
            DiskImage *dimg = active_disk();
            switch (s_wd.currentop) {
                case COP_READ_SECTOR_FUDGE:
                case COP_READ_SECTORS_FUDGE:
                    return 0u;

                case COP_READ_SECTOR:
                case COP_READ_SECTORS:
                    if (!s_wd.currsector || !dimg || !sector_data_in_bounds(dimg, s_wd.currsector, s_wd.currseclen)) {
                        s_wd.r_status &= (uint8_t)~WSF_DRQ;
                        s_wd.r_status |= WSF_RNF;
                        md_clr_drq();
                        s_wd.currentop = COP_NUFFINK;
                        break;
                    }

                    if (s_wd.curroffs == 0u) {
                        s_wd.sectype = (s_wd.currsector->data_ptr[s_wd.curroffs++] == 0xF8u) ? WSFR_RECTYP : 0x00u;
                    }

                    s_wd.r_data = s_wd.currsector->data_ptr[s_wd.curroffs++];
                    s_wd.crc    = calc_crc(s_wd.crc, s_wd.r_data);

                    s_wd.r_status &= (uint8_t)~WSF_DRQ;
                    md_clr_drq();

                    if (s_wd.curroffs > s_wd.currseclen) {
                        if (s_wd.currentop == COP_READ_SECTORS) {
                            s_wd.r_sector++;
                            s_wd.curroffs   = 0u;
                            s_wd.currsector = wd_find_sector(s_wd.r_sector);
                            s_wd.crc        = 0xE295u;

                            if (!s_wd.currsector || !sector_data_in_bounds(dimg, s_wd.currsector, (uint16_t)(1u << ((s_wd.currsector->id_ptr[4] <= 5u ? s_wd.currsector->id_ptr[4] : 1u) + 7u)))) {
                                s_wd.delayedint = 20;
                                s_wd.distatus   = s_wd.sectype;
                                s_wd.currentop  = COP_NUFFINK;
                                s_wd.r_status  &= (uint8_t)~WSF_DRQ;
                                md_clr_drq();
                                break;
                            }
                            uint8_t n2 = s_wd.currsector->id_ptr[4];
                            s_wd.currseclen = (n2 <= 5u) ? (uint16_t)(1u << (n2 + 7u)) : 256u;
                            s_wd.delayeddrq = 180;
                            break;
                        }

                        s_wd.delayedint = 32;
                        s_wd.distatus   = s_wd.sectype;
                        s_wd.currentop  = COP_NUFFINK;
                        s_wd.r_status  &= (uint8_t)~WSF_DRQ;
                        md_clr_drq();
                    } else {
                        s_wd.delayeddrq = 32;
                    }
                    break;

                case COP_READ_TRACK: {
                    uint8_t *trk = track_ptr(dimg, s_wd.c_side, s_wd.c_track);
                    if (!trk || s_wd.curroffs >= MICRODISC_TRACK_SIZE) {
                        s_wd.delayedint = 20;
                        s_wd.distatus   = 0;
                        s_wd.currentop  = COP_NUFFINK;
                        s_wd.r_status  &= (uint8_t)~WSF_DRQ;
                        md_clr_drq();
                        break;
                    }
                    s_wd.r_data = trk[s_wd.curroffs++];
                    s_wd.r_status &= (uint8_t)~WSF_DRQ;
                    md_clr_drq();

                    if (s_wd.curroffs >= MICRODISC_TRACK_SIZE) {
                        s_wd.delayedint = 20;
                        s_wd.distatus   = 0;
                        s_wd.currentop  = COP_NUFFINK;
                    } else {
                        s_wd.delayeddrq = 32;
                    }
                    break;
                }

                case COP_READ_ADDRESS:
                    if (!s_wd.currsector || !s_wd.currsector->id_ptr) {
                        s_wd.r_status &= (uint8_t)~WSF_DRQ;
                        md_clr_drq();
                        s_wd.currentop = COP_NUFFINK;
                        break;
                    }
                    if (s_wd.curroffs == 0u) {
                        s_wd.r_sector = s_wd.currsector->id_ptr[1];
                    }
                    s_wd.r_data = s_wd.currsector->id_ptr[++s_wd.curroffs];
                    s_wd.r_status &= (uint8_t)~WSF_DRQ;
                    md_clr_drq();
                    if (s_wd.curroffs >= 6u) {
                        s_wd.delayedint = 20;
                        s_wd.distatus   = 0;
                        s_wd.currentop  = COP_NUFFINK;
                    } else {
                        s_wd.delayeddrq = 32;
                    }
                    break;

                default:
                    break;
            }
            return s_wd.r_data;
        }

        default:
            return 0u;
    }
}

static void __not_in_flash_func(wd_write_reg)(uint16_t reg, uint8_t data, uint16_t cpu_op_pc)
{
    switch (reg) {
        case 0u:
            md_clr_intrq();
            switch (data & 0xE0u) {
                case 0x00u:
                    if ((data & 0x10u) == 0u) { /* Restore */
                        s_wd.r_status = WSF_BUSY;
                        if (data & 0x08u) {
                            s_wd.r_status |= WSFI_HEADL;
                        }
                        wd_seek_track(0u, cpu_op_pc == 0xE3A1u);
                        s_wd.currentop = COP_NUFFINK;
                    } else { /* Seek */
                        s_wd.r_status = WSF_BUSY;
                        if (data & 0x08u) {
                            s_wd.r_status |= WSFI_HEADL;
                        }
                        wd_seek_track(s_wd.r_data, false);
                        s_wd.currentop = COP_NUFFINK;
                    }
                    break;

                case 0x20u: /* Step */
                    s_wd.r_status = WSF_BUSY;
                    if (data & 0x08u) {
                        s_wd.r_status |= WSFI_HEADL;
                    }
                    if (s_wd.last_step_in) {
                        wd_seek_track((uint8_t)(s_wd.c_track + 1u), false);
                    } else {
                        wd_seek_track((s_wd.c_track > 0u) ? (uint8_t)(s_wd.c_track - 1u) : 0u, false);
                    }
                    s_wd.currentop = COP_NUFFINK;
                    break;

                case 0x40u: /* Step-In */
                    s_wd.r_status = WSF_BUSY;
                    if (data & 0x08u) {
                        s_wd.r_status |= WSFI_HEADL;
                    }
                    wd_seek_track((uint8_t)(s_wd.c_track + 1u), false);
                    s_wd.last_step_in = true;
                    s_wd.currentop    = COP_NUFFINK;
                    break;

                case 0x60u: /* Step-Out */
                    s_wd.r_status = WSF_BUSY;
                    if (data & 0x08u) {
                        s_wd.r_status |= WSFI_HEADL;
                    }
                    if (s_wd.c_track > 0u) {
                        wd_seek_track((uint8_t)(s_wd.c_track - 1u), false);
                    }
                    s_wd.last_step_in = false;
                    s_wd.currentop    = COP_NUFFINK;
                    break;

                case 0x80u: /* Read sector(s) */
                    start_read_sector(data, true);
                    break;

                case 0xA0u: { /* Write sector(s) */
                    DiskImage *dimg = active_disk();
                    s_wd.curroffs   = 0u;
                    s_wd.currsector = wd_find_sector(s_wd.r_sector);
                    if (!s_wd.currsector || !dimg) {
                        s_wd.r_status  = WSF_RNF;
                        md_clr_drq();
                        md_set_intrq();
                        s_wd.currentop = COP_NUFFINK;
                        break;
                    }
                    uint8_t n = s_wd.currsector->id_ptr[4];
                    s_wd.currseclen = (n <= 5u) ? (uint16_t)(1u << (n + 7u)) : 256u;
                    if (!sector_data_in_bounds(dimg, s_wd.currsector, s_wd.currseclen)) {
                        s_wd.r_status  = WSF_RNF;
                        md_clr_drq();
                        md_set_intrq();
                        s_wd.currentop = COP_NUFFINK;
                        break;
                    }
                    s_wd.r_status   = (uint8_t)(WSF_BUSY | WSF_NOTREADY);
                    s_wd.delayeddrq = 500;
                    s_wd.currentop  = (data & 0x10u) ? COP_WRITE_SECTORS : COP_WRITE_SECTOR;
                    s_wd.crc        = 0xE295u;
                    break;
                }

                case 0xC0u:
                    if ((data & 0x10u) == 0u) { /* Read address */
                        s_wd.curroffs   = 0u;
                        s_wd.currsector = s_wd.currsector ? wd_next_sector() : wd_first_sector();
                        if (!s_wd.currsector) {
                            s_wd.r_status  = WSF_RNF;
                            md_clr_drq();
                            s_wd.currentop = COP_NUFFINK;
                            md_set_intrq();
                            break;
                        }
                        s_wd.r_status = (uint8_t)(WSF_NOTREADY | WSF_BUSY | WSF_DRQ);
                        md_set_drq();
                        s_wd.currentop = COP_READ_ADDRESS;
                    } else { /* Force Interrupt */
                        s_wd.r_status   = 0u;
                        md_clr_drq();
                        md_set_intrq();
                        s_wd.delayedint = 0;
                        s_wd.delayeddrq = 0;
                        s_wd.currentop  = COP_NUFFINK;
                    }
                    break;

                case 0xE0u:
                    if ((data & 0x10u) == 0u) { /* Read track */
                        s_wd.curroffs   = 0u;
                        s_wd.r_status   = (uint8_t)(WSF_BUSY | WSF_NOTREADY);
                        s_wd.delayeddrq = 60;
                        s_wd.currentop  = COP_READ_TRACK;
                    } else { /* Write track (format) */
                        s_wd.curroffs   = 0u;
                        s_wd.r_status   = (uint8_t)(WSF_NOTREADY | WSF_BUSY);
                        s_wd.delayeddrq = 500;
                        md_clr_drq();
                        md_clr_intrq();
                        s_wd.delayedint = 0;
                        s_wd.currentop  = COP_WRITE_TRACK;
                    }
                    break;

                default:
                    break;
            }
            break;

        case 1u:
            s_wd.r_track = data;
            break;

        case 2u:
            s_wd.r_sector = data;
            break;

        case 3u: {
            s_wd.r_data = data;
            DiskImage *dimg = active_disk();

            switch (s_wd.currentop) {
                case COP_WRITE_SECTOR:
                case COP_WRITE_SECTORS:
                    if (!s_wd.currsector || !dimg ||
                        !sector_data_in_bounds(dimg, s_wd.currsector, s_wd.currseclen)) {
                        s_wd.r_status &= (uint8_t)~WSF_DRQ;
                        s_wd.r_status |= WSF_RNF;
                        md_clr_drq();
                        s_wd.currentop = COP_NUFFINK;
                        break;
                    }

                    if (s_wd.curroffs == 0u) {
                        s_wd.currsector->data_ptr[s_wd.curroffs++] = 0xFBu;
                    }
                    s_wd.currsector->data_ptr[s_wd.curroffs++] = s_wd.r_data;
                    s_wd.crc = calc_crc(s_wd.crc, s_wd.r_data);
                    disk_set_modified(dimg, s_wd.c_side, s_wd.c_track);

                    s_wd.r_status &= (uint8_t)~WSF_DRQ;
                    md_clr_drq();

                    if (s_wd.curroffs > s_wd.currseclen) {
                        s_wd.currsector->data_ptr[s_wd.curroffs++] = (uint8_t)(s_wd.crc >> 8);
                        s_wd.currsector->data_ptr[s_wd.curroffs++] = (uint8_t)(s_wd.crc & 0xFFu);

                        if (s_wd.currentop == COP_WRITE_SECTORS) {
                            s_wd.r_sector++;
                            s_wd.curroffs   = 0u;
                            s_wd.currsector = wd_find_sector(s_wd.r_sector);
                            s_wd.crc        = 0xE295u;
                            if (!s_wd.currsector) {
                                s_wd.delayedint = 20;
                                s_wd.distatus   = s_wd.sectype;
                                s_wd.currentop  = COP_NUFFINK;
                                s_wd.r_status  &= (uint8_t)~WSF_DRQ;
                                md_clr_drq();
                                break;
                            }
                            uint8_t n2 = s_wd.currsector->id_ptr[4];
                            s_wd.currseclen = (n2 <= 5u) ? (uint16_t)(1u << (n2 + 7u)) : 256u;
                            s_wd.delayeddrq = 180;
                            break;
                        }

                        s_wd.delayedint = 32;
                        s_wd.distatus   = s_wd.sectype;
                        s_wd.currentop  = COP_NUFFINK;
                        s_wd.r_status  &= (uint8_t)~WSF_DRQ;
                        md_clr_drq();
                    } else {
                        s_wd.delayeddrq = 32;
                    }
                    break;

                case COP_WRITE_TRACK: {
                    uint8_t *trk = track_ptr(dimg, s_wd.c_side, s_wd.c_track);
                    if (!trk || s_wd.curroffs >= MICRODISC_TRACK_SIZE) {
                        s_wd.delayedint = 32;
                        s_wd.currentop  = COP_NUFFINK;
                        s_wd.r_status   = 0u;
                        md_clr_drq();
                        break;
                    }

                    switch (data) {
                        case 0xF5u:
                            s_wd.crc    = 0x968Bu;
                            s_wd.r_data = 0xA1u;
                            s_wd.crc    = calc_crc(s_wd.crc, s_wd.r_data);
                            break;
                        case 0xF6u:
                            s_wd.r_data = 0xC2u;
                            s_wd.crc    = calc_crc(s_wd.crc, s_wd.r_data);
                            break;
                        case 0xF7u:
                            if (s_wd.curroffs + 1u < MICRODISC_TRACK_SIZE) {
                                trk[s_wd.curroffs++] = (uint8_t)(s_wd.crc >> 8);
                            }
                            s_wd.r_data = (uint8_t)(s_wd.crc & 0xFFu);
                            break;
                        default:
                            s_wd.crc = calc_crc(s_wd.crc, s_wd.r_data);
                            break;
                    }

                    if (s_wd.curroffs < MICRODISC_TRACK_SIZE) {
                        trk[s_wd.curroffs++] = s_wd.r_data;
                    }
                    s_wd.r_status &= (uint8_t)~WSF_DRQ;
                    md_clr_drq();

                    disk_set_modified(dimg, s_wd.c_side, s_wd.c_track);

                    if (s_wd.curroffs >= MICRODISC_TRACK_SIZE) {
                        dimg->cachedtrack = -1;
                        dimg->cachedside  = -1;
                        s_wd.delayedint   = 32;
                        s_wd.currentop    = COP_NUFFINK;
                        s_wd.r_status     = 0u;
                        md_clr_drq();
                    } else {
                        s_wd.delayeddrq = 64;
                    }
                    break;
                }

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }
}

/* --------------------------------------------------------------------------
 * API publique Microdisc
 * -------------------------------------------------------------------------- */
void microdisc_init(void)
{
    memset(s_disks, 0, sizeof(s_disks));
    for (uint8_t i = 0u; i < MICRODISC_MAX_DRIVES; i++) {
        s_disks[i].cachedtrack = -1;
        s_disks[i].cachedside  = -1;
        s_disks[i].rawimage    = get_drive_raw_buffer(i);
    }
    s_md.enabled = false;
    microdisc_reset();
}

void microdisc_reset(void)
{
    wd_init();
    s_md.status   = 0u;
    s_md.intrq    = 0u;
    s_md.drq      = 0u;
    s_md.romdis   = true;
    s_md.diskrom  = true;
    s_md.irq_line = false;

    for (uint8_t i = 0u; i < MICRODISC_MAX_DRIVES; i++) {
        s_disks[i].cachedtrack = -1;
        s_disks[i].cachedside  = -1;
    }
}

void microdisc_set_enabled(bool enabled)
{
    if (s_md.enabled == enabled) {
        return;
    }
    if (!enabled) {
        for (uint8_t i = 0u; i < MICRODISC_MAX_DRIVES; i++) {
            if (s_disks[i].mounted && s_disks[i].modified) {
                (void)microdisc_save_disk(i);
            }
        }
    }
    s_md.enabled = enabled;
    microdisc_reset();
}

bool microdisc_is_enabled(void)
{
    return s_md.enabled;
}

bool __not_in_flash_func(microdisc_romdis)(void)
{
    return s_md.enabled && s_md.romdis;
}

bool __not_in_flash_func(microdisc_diskrom)(void)
{
    return s_md.enabled && s_md.diskrom;
}

bool __not_in_flash_func(microdisc_irq_pending)(void)
{
    return s_md.enabled && s_md.irq_line;
}

uint8_t __not_in_flash_func(microdisc_read)(uint16_t addr)
{
    if (addr >= 0x0310u && addr < 0x0314u) {
        return wd_read_reg((uint16_t)(addr & 3u));
    }
    if (addr == 0x0314u) {
        return (uint8_t)(s_md.intrq | 0x7Fu);
    }
    if (addr == 0x0318u) {
        return (uint8_t)(s_md.drq | 0x7Fu);
    }
    return 0xFFu;
}

void __not_in_flash_func(microdisc_write)(uint16_t addr, uint8_t data, uint16_t cpu_op_pc)
{
    if (addr >= 0x0310u && addr < 0x0314u) {
        wd_write_reg((uint16_t)(addr & 3u), data, cpu_op_pc);
        return;
    }
    if (addr == 0x0314u) {
        s_md.status = data;
        s_md.irq_line = ((data & MDSF_INTENA) != 0u && s_md.intrq == 0u);
        s_wd.c_drive  = (uint8_t)((data & MDSF_DRIVE) >> 5);
        s_wd.c_side   = (data & MDSF_SIDE) ? 1u : 0u;
        s_md.romdis   = (data & MDSF_ROMDIS) == 0u;
        s_md.diskrom  = (data & MDSF_EPROM) == 0u;
        return;
    }
    if (addr == 0x0318u) {
        s_md.drq = (uint8_t)(data & MF_DRQ);
    }
}

void __not_in_flash_func(microdisc_tick)(uint8_t cycles)
{
    if (!s_md.enabled) {
        return;
    }

    if (s_wd.currentop == COP_READ_SECTORS_FUDGE ||
        s_wd.currentop == COP_READ_SECTOR_FUDGE) {
        if (active_disk()) {
            start_read_sector((s_wd.currentop == COP_READ_SECTORS_FUDGE) ? 0x90u : 0x80u, false);
        }
    }

    if (s_wd.delayedint > 0) {
        s_wd.delayedint -= (int32_t)cycles;
        if (s_wd.delayedint <= 0) {
            s_wd.delayedint = 0;
            if (s_wd.distatus != -1) {
                s_wd.r_status = (uint8_t)s_wd.distatus;
                s_wd.distatus = -1;
            }
            md_set_intrq();
        }
    }

    if (s_wd.delayeddrq > 0) {
        s_wd.delayeddrq -= (int32_t)cycles;
        if (s_wd.delayeddrq <= 0) {
            s_wd.delayeddrq = 0;
            if (s_wd.ddstatus != -1) {
                s_wd.r_status = (uint8_t)s_wd.ddstatus;
                s_wd.ddstatus = -1;
            }
            s_wd.r_status |= WSF_DRQ;
            md_set_drq();
        }
    }

    if (s_wd.currentop == COP_NUFFINK) {
        for (uint8_t i = 0u; i < MICRODISC_MAX_DRIVES; i++) {
            if (s_disks[i].mounted && s_disks[i].modified && !s_disks[i].autoflush_tried) {
                if (s_dirtyIdleCycles < 3000000UL) {
                    s_dirtyIdleCycles += cycles;
                }
                break;
            }
        }
    }
}

bool microdisc_should_autoflush(void)
{
    if (!s_md.enabled || s_wd.currentop != COP_NUFFINK || s_dirtyIdleCycles < MICRODISC_AUTOFLUSH_CYCLES) {
        return false;
    }
    if (!hal_sd_is_ready()) {
        return false;
    }
    for (uint8_t i = 0u; i < MICRODISC_MAX_DRIVES; i++) {
        if (s_disks[i].mounted && s_disks[i].modified && !s_disks[i].autoflush_tried) {
            return true;
        }
    }
    return false;
}

void microdisc_autoflush(void)
{
    for (uint8_t i = 0u; i < MICRODISC_MAX_DRIVES; i++) {
        if (s_disks[i].mounted && s_disks[i].modified && !s_disks[i].autoflush_tried) {
            s_disks[i].autoflush_tried = true;
            (void)microdisc_save_disk(i);
        }
    }
    s_dirtyIdleCycles = 0u;
}

/* --------------------------------------------------------------------------
 * Chargement / Normalisation / Sauvegarde d'images MFM_DISK (.DSK)
 * -------------------------------------------------------------------------- */
static bool finalize_disk_mount(uint8_t drive, const char *name, uint32_t loaded_len)
{
    DiskImage *dimg = &s_disks[drive];
    uint8_t   *raw  = dimg->rawimage;

    if (loaded_len < MICRODISC_HEADER_SIZE) {
        return false;
    }

    static const char kMagic[] = "MFM_DISK";
    for (int i = 0; i < 8; i++) {
        if (raw[i] != (uint8_t)kMagic[i]) {
            return false;
        }
    }

    uint32_t numsides_in  = raw_get_u32(raw, 8u);
    uint32_t numtracks_in = raw_get_u32(raw, 12u);
    uint32_t geometry     = raw_get_u32(raw, 16u);

    if (geometry != 1u ||
        numsides_in < 1u || numsides_in > 2u ||
        numtracks_in == 0u || numtracks_in > MICRODISC_MAX_TRACKS) {
        return false;
    }

    if (numsides_in > 1u && numtracks_in < MICRODISC_MAX_TRACKS) {
        uint32_t size = numtracks_in * MICRODISC_TRACK_SIZE;
        uint8_t *src  = raw + MICRODISC_HEADER_SIZE + size;
        uint8_t *dst  = raw + MICRODISC_HEADER_SIZE + MICRODISC_MAX_TRACKS * MICRODISC_TRACK_SIZE;
        raw_move_backward(dst, src, size);
        raw_zero_bytes(src, (uint32_t)(dst - src));
    }

    dimg->mounted         = true;
    dimg->modified        = false;
    dimg->autoflush_tried = false;
    dimg->numsides        = 2u;
    dimg->numtracks       = (uint8_t)MICRODISC_MAX_TRACKS;
    dimg->numsides_in     = numsides_in;
    dimg->numtracks_in    = numtracks_in;
    dimg->numsides_out    = 0u;
    dimg->numtracks_out   = 0u;
    dimg->geometry        = geometry;
    dimg->cachedtrack     = -1;
    dimg->cachedside      = -1;
    dimg->numsectors      = 0u;

    size_t nlen = strlen(name);
    if (nlen >= MICRODISC_NAME_LEN) {
        nlen = MICRODISC_NAME_LEN - 1u;
    }
    memcpy(dimg->filename, name, nlen);
    dimg->filename[nlen] = '\0';

    s_dirtyIdleCycles = 0u;
    return true;
}

void microdisc_eject_disk(uint8_t drive, bool save_if_modified)
{
    if (drive >= MICRODISC_MAX_DRIVES) {
        return;
    }
    DiskImage *dimg = &s_disks[drive];
    if (dimg->mounted && save_if_modified && dimg->modified) {
        (void)microdisc_save_disk(drive);
    }
    uint8_t *raw = dimg->rawimage;
    memset(dimg, 0, sizeof(*dimg));
    dimg->rawimage    = raw;
    dimg->cachedtrack = -1;
    dimg->cachedside  = -1;
    if (s_wd.c_drive == drive) {
        s_wd.currsector = NULL;
    }
}

bool microdisc_insert_disk(uint8_t drive, const char *filename)
{
    if (drive >= MICRODISC_MAX_DRIVES || !filename || !filename[0]) {
        return false;
    }
    uint8_t *raw = get_drive_raw_buffer(drive);
    if (!raw) {
        return false;
    }

    /* Sauver d'abord une disquette deja modifiee avant d'ecraser le tampon */
    if (s_disks[drive].mounted && s_disks[drive].modified) {
        (void)microdisc_save_disk(drive);
    }

    uint32_t fsize = 0u;
    HalFileHandle hf = hal_file_open_read(filename, &fsize);
    if (!hf) {
        return false;
    }
    if (fsize < MICRODISC_HEADER_SIZE || fsize > MICRODISC_RAW_LEN) {
        hal_file_close_read(hf);
        return false;
    }

    microdisc_eject_disk(drive, false);
    s_disks[drive].rawimage = raw;
    raw_zero_bytes(raw, MICRODISC_RAW_LEN);

    uint32_t pos = 0u;
    while (pos < fsize) {
        uint32_t todo = fsize - pos;
        if (todo > (uint32_t)sizeof(s_ioBounce)) {
            todo = (uint32_t)sizeof(s_ioBounce);
        }
        size_t got = hal_file_read(hf, s_ioBounce, todo);
        if (got == 0u) {
            break;
        }
        raw_copy_bytes(raw + pos, s_ioBounce, (uint32_t)got);
        pos += (uint32_t)got;
    }
    hal_file_close_read(hf);

    if (pos != fsize || !finalize_disk_mount(drive, filename, fsize)) {
        microdisc_eject_disk(drive, false);
        return false;
    }
    return true;
}

bool microdisc_insert_image_mem(uint8_t drive, const char *name, const uint8_t *data, uint32_t len)
{
    if (drive >= MICRODISC_MAX_DRIVES || !name || !data ||
        len < MICRODISC_HEADER_SIZE || len > MICRODISC_RAW_LEN) {
        return false;
    }
    uint8_t *raw = get_drive_raw_buffer(drive);
    if (!raw) {
        return false;
    }
    microdisc_eject_disk(drive, false);
    s_disks[drive].rawimage = raw;
    raw_zero_bytes(raw, MICRODISC_RAW_LEN);
    raw_copy_bytes(raw, data, len);
    if (!finalize_disk_mount(drive, name, len)) {
        microdisc_eject_disk(drive, false);
        return false;
    }
    return true;
}

static bool write_raw_chunked(HalFileHandle hf, const uint8_t *src, uint32_t total)
{
    uint32_t pos = 0u;
    while (pos < total) {
        uint32_t todo = total - pos;
        if (todo > (uint32_t)sizeof(s_ioBounce)) {
            todo = (uint32_t)sizeof(s_ioBounce);
        }
        raw_copy_bytes(s_ioBounce, src + pos, todo);
        if (!hal_file_write(hf, s_ioBounce, todo)) {
            return false;
        }
        pos += todo;
    }
    return true;
}

bool microdisc_save_disk(uint8_t drive)
{
    if (drive >= MICRODISC_MAX_DRIVES) {
        return false;
    }
    DiskImage *dimg = &s_disks[drive];
    if (!dimg->mounted || !dimg->rawimage || !dimg->filename[0]) {
        return false;
    }

    if (dimg->numsides_in < dimg->numsides_out) {
        dimg->numsides_in = dimg->numsides_out;
    }
    if (dimg->numtracks_in < dimg->numtracks_out) {
        dimg->numtracks_in = dimg->numtracks_out;
    }

    raw_set_u32(dimg->rawimage, 8u, dimg->numsides_in);
    raw_set_u32(dimg->rawimage, 12u, dimg->numtracks_in);

    uint32_t sidesize = dimg->numtracks_in * MICRODISC_TRACK_SIZE;
    HalFileHandle hf = hal_file_open_write(dimg->filename);
    if (!hf) {
        return false;
    }

    bool ok = write_raw_chunked(hf, dimg->rawimage, MICRODISC_HEADER_SIZE + sidesize);
    if (ok && dimg->numsides_in > 1u) {
        const uint8_t *side1 = dimg->rawimage + MICRODISC_HEADER_SIZE + MICRODISC_MAX_TRACKS * MICRODISC_TRACK_SIZE;
        ok = write_raw_chunked(hf, side1, sidesize);
    }
    hal_file_close(hf);

    if (ok) {
        dimg->modified        = false;
        dimg->autoflush_tried = false;
        s_dirtyIdleCycles     = 0u;
    }
    return ok;
}

bool microdisc_has_disk(uint8_t drive)
{
    return (drive < MICRODISC_MAX_DRIVES) && s_disks[drive].mounted;
}

bool microdisc_is_modified(uint8_t drive)
{
    return (drive < MICRODISC_MAX_DRIVES) && s_disks[drive].mounted && s_disks[drive].modified;
}

const char *microdisc_disk_name(uint8_t drive)
{
    if (drive >= MICRODISC_MAX_DRIVES || !s_disks[drive].mounted) {
        return "";
    }
    return s_disks[drive].filename;
}

uint8_t microdisc_current_drive(void)
{
    return s_wd.c_drive;
}

uint8_t microdisc_current_track(void)
{
    return s_wd.c_track;
}

uint8_t microdisc_status_reg(void)
{
    return s_md.status;
}

uint8_t microdisc_wd_status_reg(void)
{
    return s_wd.r_status;
}

bool microdisc_storage_ready(void)
{
    return get_drive_raw_buffer(0u) != NULL;
}
