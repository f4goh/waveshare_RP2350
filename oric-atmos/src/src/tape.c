/*
 * Gestionnaire cassette TAP en streaming SD + menu selection Oric
 * Economise 64 Ko de RAM RP2040 grace a la lecture stream 512 octets via arduino_hal.
 */

#include "tape.h"
#include "arduino_hal.h"
#include "config.h"
#include "input_kbd.h"
#include "nes_ctrl.h"
#include "oric_keyboard.h"
#include "ps2_kbd.h"

#include "pico/time.h"

#include <stdio.h>
#include <string.h>

#define FILES_FIRST_ROW   1U
#define FILES_ROWS        25U
#define FILES_PER_PAGE    (FILES_ROWS * 2U) /* 50 fichiers par page */
#define COL1_X            4U
#define COL2_X            23U
#define CURSOR_COL1_X     3U
#define CURSOR_COL2_X     22U
#define MAX_DISP_NAME     16U

/* Etat de decodage de bloc TAP a la volee */
static bool     s_sawSync24    = false;
static uint8_t  s_hdrIdx       = 0;
static bool     s_inFilename   = false;
static uint8_t  s_hdrBytes[9];
static uint32_t s_payloadRem   = 0;
static uint8_t  s_lastIndex    = 0;

static char s_fileList[SD_MAX_LIST_FILES][SD_MAX_FILENAME_LEN];
static char s_currentTapeName[SD_MAX_FILENAME_LEN];

char (*tape_shared_file_list(void))[SD_MAX_FILENAME_LEN]
{
    return s_fileList;
}

void tape_init(void)
{
    tape_reset();
}

void tape_reset(void)
{
    hal_tape_close();
    s_sawSync24          = false;
    s_hdrIdx             = 0;
    s_inFilename         = false;
    s_payloadRem         = 0;
    s_currentTapeName[0] = '\0';
}

bool tape_is_active(void)
{
    return hal_tape_is_open();
}

const char *tape_current_name(void)
{
    return hal_tape_is_open() ? s_currentTapeName : "";
}

bool tape_insert_file(const char *base_name)
{
    if (!base_name || !base_name[0]) {
        return false;
    }
    char fullpath[SD_MAX_FILENAME_LEN + 8];
    snprintf(fullpath, sizeof(fullpath), "%s.tap", base_name);
    if (!hal_tape_open(fullpath)) {
        return false;
    }
    oric_keyboard_load_nes_csv(base_name);
    strncpy(s_currentTapeName, base_name, sizeof(s_currentTapeName) - 1u);
    s_currentTapeName[sizeof(s_currentTapeName) - 1u] = '\0';
    s_sawSync24  = false;
    s_hdrIdx     = 0;
    s_inFilename = false;
    s_payloadRem = 0;
    return true;
}

uint32_t tape_bytes_remaining(void)
{
    if (!hal_tape_is_open()) {
        return 0;
    }
    uint32_t sz  = hal_tape_size();
    uint32_t pos = hal_tape_tell();
    return (sz > pos) ? (sz - pos) : 0;
}

static void draw_menu_frame(uint8_t *oric_ram, size_t page, size_t total_pages)
{
    uint8_t *scr = &oric_ram[ORIC_SCREEN_ADDR];

    for (uint8_t y = 0; y < ORIC_SCREEN_ROWS; y++) {
        uint8_t *row = scr + (y * ORIC_SCREEN_COLS);
        row[0] = 0x10u; /* PAPER black */
        row[1] = 0x03u; /* INK yellow */
        for (uint8_t x = 2; x < ORIC_SCREEN_COLS; x++) {
            row[x] = ' ';
        }
    }

    scr[0] = 0x14u; /* PAPER blue */
    scr[1] = 0x07u; /* INK white */

    char header[48];
    snprintf(header, sizeof(header), "SELECT TAP (%u/%u)      F4GOH 2026",
             (unsigned)(page + 1u), (unsigned)total_pages);
    for (size_t i = 0; header[i] && (3u + i) < ORIC_SCREEN_COLS; i++) {
        scr[3u + i] = (uint8_t)header[i];
    }

    /* Ligne d'aide en bas */
    uint8_t *bot = scr + ((ORIC_SCREEN_ROWS - 1u) * ORIC_SCREEN_COLS);
    bot[0] = 0x14u;
    bot[1] = 0x06u;
    const char *help = "ARROWS:MOVE   ENTER/SPACE:LOAD";
    for (size_t i = 0; help[i] && (3u + i) < ORIC_SCREEN_COLS; i++) {
        bot[3u + i] = (uint8_t)help[i];
    }
}

static void draw_menu_page(uint8_t *oric_ram, int total_files, size_t sel)
{
    size_t total_pages = ((size_t)total_files + FILES_PER_PAGE - 1u) / FILES_PER_PAGE;
    if (total_pages == 0) total_pages = 1;
    size_t page = sel / FILES_PER_PAGE;

    draw_menu_frame(oric_ram, page, total_pages);

    uint8_t *scr = &oric_ram[ORIC_SCREEN_ADDR];
    size_t start = page * FILES_PER_PAGE;
    size_t end   = start + FILES_PER_PAGE;
    if (end > (size_t)total_files) {
        end = (size_t)total_files;
    }

    for (size_t i = start; i < end; i++) {
        size_t idx_in_page = i - start;
        size_t row         = FILES_FIRST_ROW + (idx_in_page / 2u);
        size_t col         = ((idx_in_page & 1u) == 0u) ? COL1_X : COL2_X;
        size_t ccol        = ((idx_in_page & 1u) == 0u) ? CURSOR_COL1_X : CURSOR_COL2_X;
        uint8_t *line      = scr + (row * ORIC_SCREEN_COLS);

        line[ccol] = (i == sel) ? '>' : ' ';

        const char *name = s_fileList[i];
        for (size_t j = 0; name[j] && j < MAX_DISP_NAME; j++) {
            line[col + j] = (uint8_t)name[j];
        }
    }
}

static bool tape_open_menu(uint8_t *oric_ram)
{
    int nb_files = hal_sd_list_files(".tap", s_fileList, SD_MAX_LIST_FILES, true);
    if (nb_files <= 0) {
        uint8_t *scr = &oric_ram[ORIC_SCREEN_ADDR];
        draw_menu_frame(oric_ram, 0, 1);
        const char *msg = (nb_files < 0) ? "ERREUR CARTE SD!" : "AUCUN FICHIER .TAP";
        for (size_t i = 0; msg[i]; i++) {
            scr[12u * ORIC_SCREEN_COLS + 10u + i] = (uint8_t)msg[i];
        }
        sleep_ms(1200);
        return false;
    }

    size_t sel = (s_lastIndex < (size_t)nb_files) ? s_lastIndex : 0u;
    draw_menu_page(oric_ram, nb_files, sel);

    input_kbd_clear();

    uint8_t  nesOld     = nes_read_blocking();
    uint32_t lastNesUs  = time_us_32();
    bool     chosen     = false;

    while (!chosen) {
        size_t prev_sel = sel;

        uint32_t now = time_us_32();
        if ((uint32_t)(now - lastNesUs) >= 20000u) {
            lastNesUs = now;
            uint8_t nes = nes_read_blocking();
            if (nes != nesOld) {
                if      ((nes & 0x01u) == 0u && sel + 1u < (size_t)nb_files) sel++;
                else if ((nes & 0x02u) == 0u && sel > 0u)                    sel--;
                else if ((nes & 0x04u) == 0u && sel + 2u < (size_t)nb_files) sel += 2u;
                else if ((nes & 0x08u) == 0u && sel >= 2u)                   sel -= 2u;
                else if ((nes & 0x80u) == 0u || (nes & 0x10u) == 0u)         chosen = true;
                nesOld = nes;
            }
        }

        Ps2KeyEvent ev;
        while (!chosen && input_kbd_poll(&ev)) {
            if (!ev.pressed) {
                continue;
            }
            if (ev.code == 0x5Au || ev.code == 0x29u) { /* Enter / Space */
                chosen = true;
                break;
            } else if (ev.code == 0x74u && sel + 1u < (size_t)nb_files) { /* Right */
                sel++;
            } else if (ev.code == 0x6Bu && sel > 0u) { /* Left */
                sel--;
            } else if (ev.code == 0x72u && ev.extended && sel + 2u < (size_t)nb_files) { /* Down */
                sel += 2u;
            } else if (ev.code == 0x75u && ev.extended && sel >= 2u) { /* Up */
                sel -= 2u;
            }
        }

        if (sel != prev_sel) {
            draw_menu_page(oric_ram, nb_files, sel);
        }
    }

    s_lastIndex = (uint8_t)sel;
    input_kbd_clear();
    oric_keyboard_clear();

    return tape_insert_file(s_fileList[sel]);
}

void tape_on_e4ac(uint8_t *oric_ram)
{
    if (!hal_tape_is_open()) {
        tape_open_menu(oric_ram);
    }
}

void tape_on_e735(uint8_t *oric_ram)
{
    if (!hal_tape_is_open()) {
        if (!tape_open_menu(oric_ram)) {
            return;
        }
    }

    /* Si un bloc precedent a ete rejete (ex: CLOAD "NOM"), saute son payload */
    if (s_payloadRem > 0u) {
        uint32_t target = hal_tape_tell() + s_payloadRem;
        if (target >= hal_tape_size()) {
            tape_reset();
            return;
        }
        hal_tape_seek(target);
        s_payloadRem = 0;
    }

    /* Avance jusqu'au marqueur 0x24 suivant les octets de synchro 0x16 */
    uint8_t b = 0;
    while (hal_tape_read_byte(&b)) {
        if (b == 0x24u) {
            uint32_t pos = hal_tape_tell();
            if (pos > 0u) {
                hal_tape_seek(pos - 1u);
            }
            break;
        }
    }

    s_sawSync24  = false;
    s_hdrIdx     = 0;
    s_inFilename = false;
    s_payloadRem = 0;
}

uint8_t tape_read_byte(void)
{
    if (!hal_tape_is_open()) {
        return 0;
    }

    uint8_t b = 0;
    if (!hal_tape_read_byte(&b)) {
        tape_reset();
        return 0;
    }

    if (!s_sawSync24) {
        if (b == 0x24u) {
            s_sawSync24 = true;
            s_hdrIdx    = 0;
        }
    } else if (s_hdrIdx < 9u) {
        s_hdrBytes[s_hdrIdx++] = b;
        if (s_hdrIdx == 9u) {
            s_inFilename = true;
        }
    } else if (s_inFilename) {
        if (b == 0x00u) {
            s_inFilename = false;
            /* Format header ROM ($02B0..$02A8 en ordre inverse) :
             * [0]=flag_int [1]=flag_str [2]=type [3]=autorun
             * [4]=end_hi   [5]=end_lo   [6]=start_hi [7]=start_lo [8]=reserved */
            uint16_t end_addr   = (uint16_t)(((uint16_t)s_hdrBytes[4] << 8) | s_hdrBytes[5]);
            uint16_t start_addr = (uint16_t)(((uint16_t)s_hdrBytes[6] << 8) | s_hdrBytes[7]);
            s_payloadRem = (end_addr >= start_addr)
                           ? (uint32_t)(end_addr - start_addr + 1u)
                           : 0u;
            if (s_payloadRem == 0u) {
                s_sawSync24 = false;
            }
        }
    } else if (s_payloadRem > 0u) {
        s_payloadRem--;
        if (s_payloadRem == 0u) {
            s_sawSync24 = false;
            /* Ferme automatiquement si fin de fichier (ou < 12 octets de bourrage) */
            if (tape_bytes_remaining() < 12u) {
                tape_reset();
            }
        }
    }

    return b;
}

void tape_csave(const uint8_t *oric_ram)
{
    uint8_t hdr[9];
    hdr[0] = oric_ram[0x02B0];
    hdr[1] = oric_ram[0x02AF];
    hdr[2] = oric_ram[0x02AE];
    hdr[3] = oric_ram[0x02AD];
    hdr[4] = oric_ram[0x02AC];
    hdr[5] = oric_ram[0x02AB];
    hdr[6] = oric_ram[0x02AA];
    hdr[7] = oric_ram[0x02A9];
    hdr[8] = oric_ram[0x02A8];

    char name[16];
    memset(name, 0, sizeof(name));
    if (oric_ram[0x027F] == 0) {
        strcpy(name, "save");
    } else {
        for (size_t i = 0; i < sizeof(name) - 1u; i++) {
            uint8_t c = oric_ram[0x027F + i];
            if (c == 0) break;
            name[i] = (char)c;
        }
    }

    char path[32];
    snprintf(path, sizeof(path), "%s.tap", name);

    HalFileHandle h = hal_file_open_write(path);
    if (!h) {
        return;
    }

    const uint8_t sync[4] = { 0x16u, 0x16u, 0x16u, 0x24u };
    hal_file_write(h, sync, sizeof(sync));
    hal_file_write(h, hdr, sizeof(hdr));
    hal_file_write(h, (const uint8_t *)name, strlen(name) + 1u);

    uint16_t start_addr = (uint16_t)(((uint16_t)hdr[6] << 8) | hdr[7]);
    uint16_t end_addr   = (uint16_t)(((uint16_t)hdr[4] << 8) | hdr[5]);
    if (end_addr >= start_addr && end_addr < ORIC_RAM_SIZE) {
        hal_file_write(h, &oric_ram[start_addr], (size_t)(end_addr - start_addr + 1u));
    }

    hal_file_close(h);
    oric_keyboard_clear();
}
