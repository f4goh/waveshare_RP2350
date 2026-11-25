/*
 * Super Configurateur OSD Oric Atmos (40x28 dans ramMoni)
 * Onglets :
 *   [1] FICHIERS  : Navigateur .TAP, Auto-CLOAD, BMP, Snapshots PSRAM/SD, Reset
 *   [2] REGLAGES  : Clavier AZERTY/QWERTY, Palette CRT, Audio, Manette NES, Save SD
 *   [3] MATERIEL  : RP2350, Flash JEDEC + Puya QE, PSRAM APS6404L, I2C1, NES Live
 *   [4] USB HOST  : Dual-Port rhport0+1, compteurs timeouts/rearm, peripheriques HID
 */

#include "config_menu.h"
#include "arduino_hal.h"
#include "board_hw.h"
#include "bus.h"
#include "config.h"
#include "config_store.h"
#include "input_kbd.h"
#include "microdisc.h"
#include "monitor.h"
#include "nes_ctrl.h"
#include "oric_keyboard.h"
#include "ps2_kbd.h"
#include "tape.h"
#include "usb_kbd.h"
#include "video.h"

#include "pico/time.h"

#include <stdio.h>
#include <string.h>

#define CFG_TAB_COUNT    5
#define CFG_ROW_HEADER   0
#define CFG_ROW_TABS     1
#define CFG_ROW_BODY0    3
#define CFG_BODY_LINES   22
#define CFG_ROW_STATUS   26
#define CFG_ROW_HELP     27

typedef enum {
    TAB_FILES = 0,
    TAB_SETTINGS,
    TAB_HARDWARE,
    TAB_USB,
    TAB_GAMEPAD
} CfgTab;

/* Actions Tab 0 */
enum {
    ACT_PICK_DSK = 0,
    ACT_SAVE_DSK,
    ACT_EJECT_DSK,
    ACT_BOOT_DSK,
    ACT_PICK_TAP,
    ACT_EJECT_TAP,
    ACT_AUTOCLOAD,
    ACT_RESET_CLOAD,
    ACT_BMP,
    ACT_PSRAM_SAVE,
    ACT_PSRAM_LOAD,
    ACT_SD_SNAP_SAVE,
    ACT_SD_SNAP_LOAD,
    ACT_MONITOR,
    ACT_RESET_ORIC,
    TAB_FILES_COUNT
};

/* Options Tab 1 */
enum {
    OPT_MACHINE = 0,
    OPT_AUDIO_OUT,
    OPT_AZERTY,
    OPT_PALETTE,
    OPT_AUDIO_MUTE,
    OPT_AUDIO_VOL,
    OPT_AUDIO_DC,
    OPT_NES_PRESET,
    OPT_SAVE_CFG,
    OPT_DEF_CFG,
    TAB_SETTINGS_COUNT
};

/* Lignes Tab 4 (Manette NES) : 0..7 = boutons NES, 8 = preset, 9 = sauver */
#define PAD_ROW_PRESET     8
#define PAD_ROW_SAVE       9
#define TAB_GAMEPAD_COUNT  10

typedef enum {
    BROWSER_NONE = 0,
    BROWSER_TAP,
    BROWSER_DSK
} FileBrowserMode;

static uint8_t         s_curTab       = TAB_FILES;
static uint8_t         s_selRow[CFG_TAB_COUNT] = {0};
static bool            s_padCapture   = false;
static uint8_t         s_psramSlot    = 0;
static FileBrowserMode s_browserMode  = BROWSER_NONE;
static int             s_tapCount     = 0;
static int             s_tapSel       = 0;
static int             s_tapScroll    = 0;
static char        s_statusMsg[39] = "PRET - ENTREE:VALIDER | TAB:ONGLET";
static bool        s_statusOk     = true;
static uint8_t     s_i2cFound[8];
static int         s_i2cCount     = -1;
static uint8_t     s_lastKeySc    = 0;
static bool        s_lastKeyExt   = false;
static uint8_t     s_lastKeySrc   = 0;

static void osd_set_line(uint8_t row, uint8_t paper, uint8_t ink, const char *txt)
{
    if (row >= ORIC_SCREEN_ROWS) {
        return;
    }
    uint8_t *line = &ramMoni[(size_t)row * ORIC_SCREEN_COLS];
    line[0] = (uint8_t)(0x10u | (paper & 0x07u));
    line[1] = (uint8_t)(ink & 0x07u);
    memset(&line[2], ' ', ORIC_SCREEN_COLS - 2u);
    if (txt) {
        for (size_t i = 0; txt[i] && i < ORIC_SCREEN_COLS - 2u; i++) {
            uint8_t ch = (uint8_t)txt[i];
            if (ch < 0x20u || ch >= 0x7Fu) {
                ch = ' ';
            }
            line[2u + i] = ch;
        }
    }
}

static void osd_status(bool ok, const char *msg)
{
    s_statusOk = ok;
    snprintf(s_statusMsg, sizeof(s_statusMsg), "%s", msg ? msg : "");
}

static void osd_scan_i2c(void)
{
    int cnt = 0;
    for (uint8_t addr = 0x03u; addr < 0x78u; addr++) {
        if (hal_i2c_probe(addr)) {
            if (cnt < (int)sizeof(s_i2cFound)) {
                s_i2cFound[cnt] = addr;
            }
            cnt++;
        }
    }
    s_i2cCount = cnt;
}

static void osd_draw_chrome(void)
{
    for (uint8_t r = 0; r < ORIC_SCREEN_ROWS; r++) {
        osd_set_line(r, 0u, 7u, "");
    }
    osd_set_line(CFG_ROW_HEADER, 4u, 3u, " ORIC CONFIGURATEUR   ESC/F12:SORTIR");

    char tabline[40];
    snprintf(tabline, sizeof(tabline),
             "%c1:FIC%c%c2:CFG%c%c3:MAT%c%c4:USB%c%c5:PAD%c",
             (s_curTab == 0) ? '[' : ' ', (s_curTab == 0) ? ']' : ' ',
             (s_curTab == 1) ? '[' : ' ', (s_curTab == 1) ? ']' : ' ',
             (s_curTab == 2) ? '[' : ' ', (s_curTab == 2) ? ']' : ' ',
             (s_curTab == 3) ? '[' : ' ', (s_curTab == 3) ? ']' : ' ',
             (s_curTab == 4) ? '[' : ' ', (s_curTab == 4) ? ']' : ' ');
    osd_set_line(CFG_ROW_TABS, 4u, 7u, tabline);
    osd_set_line(2u, 0u, 6u, "--------------------------------------");

    osd_set_line(CFG_ROW_STATUS, 0u, s_statusOk ? 2u : 1u, s_statusMsg);
    if (s_browserMode != BROWSER_NONE) {
        osd_set_line(CFG_ROW_HELP, 4u, 7u, "H/B:CHOISIR  ENTREE/A:MONTER  ESC:RET");
    } else if (s_curTab == TAB_GAMEPAD) {
        osd_set_line(CFG_ROW_HELP, 4u, 7u, "G/D:TOUCHE  ENTREE:CAPTURER  TAB:ONG");
    } else {
        osd_set_line(CFG_ROW_HELP, 4u, 7u, "TAB/1-5:ONGLET  H/B:LIGNE  G/D:MODIF");
    }
}

static void draw_tap_browser(void)
{
    bool is_dsk = (s_browserMode == BROWSER_DSK);
    osd_set_line(CFG_ROW_BODY0, 0u, 3u,
                 is_dsk ? "SELECTION DISQUETTE .DSK (CARTE SD):"
                        : "SELECTION CASSETTE .TAP (CARTE SD):");
    if (s_tapCount <= 0) {
        osd_set_line(CFG_ROW_BODY0 + 2u, 0u, 1u,
                     (s_tapCount < 0) ? "  ERREUR ACCES CARTE SD"
                                      : (is_dsk ? "  AUCUN FICHIER .DSK TROUVE"
                                                : "  AUCUN FICHIER .TAP TROUVE"));
        return;
    }

    const int max_vis = 18;
    if (s_tapSel < s_tapScroll) {
        s_tapScroll = s_tapSel;
    }
    if (s_tapSel >= s_tapScroll + max_vis) {
        s_tapScroll = s_tapSel - max_vis + 1;
    }

    char line[56];
    for (int i = 0; i < max_vis; i++) {
        int idx = s_tapScroll + i;
        if (idx >= s_tapCount) {
            break;
        }
        char (*list)[SD_MAX_FILENAME_LEN] = tape_shared_file_list();
        bool sel = (idx == s_tapSel);
        snprintf(line, sizeof(line), "%c %02d. %-31.31s",
                 sel ? '>' : ' ', (idx + 1) % 100, list[idx]);
        osd_set_line((uint8_t)(CFG_ROW_BODY0 + 2 + i),
                     sel ? 4u : 0u,
                     sel ? 3u : 7u,
                     line);
    }

    snprintf(line, sizeof(line), "FICHIER %d/%d", s_tapSel + 1, s_tapCount);
    osd_set_line(CFG_ROW_BODY0 + 21u, 0u, 6u, line);
}

static void draw_tab_files(void)
{
    if (s_browserMode != BROWSER_NONE) {
        draw_tap_browser();
        return;
    }

    char line[40];
    if (microdisc_has_disk(0u)) {
        snprintf(line, sizeof(line), "DSK A: %-21.21s%s",
                 microdisc_disk_name(0u),
                 microdisc_is_modified(0u) ? " [MOD]" : "");
    } else {
        snprintf(line, sizeof(line), "DSK A: (aucune disquette) [%s]",
                 microdisc_is_enabled() ? "MICRODISC" : "ATMOS");
    }
    osd_set_line(CFG_ROW_BODY0, 0u, 3u, line);

    const char *cur_tap = tape_current_name();
    if (cur_tap[0]) {
        snprintf(line, sizeof(line), "K7   : %-18.18s (%luo)",
                 cur_tap, (unsigned long)tape_bytes_remaining());
    } else {
        snprintf(line, sizeof(line), "K7   : (aucune cassette inseree)");
    }
    osd_set_line(CFG_ROW_BODY0 + 1u, 0u, 6u, line);

    bool psram_ok = (board_psram_size() >= (2UL << 20));
    bool slot_valid = monitor_psram_slot_valid(s_psramSlot);
    static const char kSlotChar[4] = {'A', 'B', 'C', 'D'};

    const char *labels[TAB_FILES_COUNT];
    char buf_ps[40], buf_pl[40];

    if (psram_ok) {
        snprintf(buf_ps, sizeof(buf_ps), "PSRAM Save : < Slot %c [%s] >",
                 kSlotChar[s_psramSlot & 3u], slot_valid ? "OCC" : "VID");
        snprintf(buf_pl, sizeof(buf_pl), "PSRAM Load : < Slot %c [%s] >",
                 kSlotChar[s_psramSlot & 3u], slot_valid ? "OK " : "---");
    } else {
        snprintf(buf_ps, sizeof(buf_ps), "PSRAM Save : (PSRAM absente)");
        snprintf(buf_pl, sizeof(buf_pl), "PSRAM Load : (PSRAM absente)");
    }

    labels[ACT_PICK_DSK]     = "Inserer disquette .DSK (A:)...";
    labels[ACT_SAVE_DSK]     = "Sauver disquette .DSK -> SD";
    labels[ACT_EJECT_DSK]    = "Ejecter disquette .DSK (A:)";
    labels[ACT_BOOT_DSK]     = "Reset Oric (Booter Microdisc)";
    labels[ACT_PICK_TAP]     = "Choisir cassette .TAP sur SD...";
    labels[ACT_EJECT_TAP]    = "Ejecter la cassette .TAP";
    labels[ACT_AUTOCLOAD]    = "Taper CLOAD\"\" dans l'Oric";
    labels[ACT_RESET_CLOAD]  = "Reset Oric + Lancer CLOAD\"\"";
    labels[ACT_BMP]          = "Capture ecran -> CAPTURE.BMP";
    labels[ACT_PSRAM_SAVE]   = buf_ps;
    labels[ACT_PSRAM_LOAD]   = buf_pl;
    labels[ACT_SD_SNAP_SAVE] = "Snapshot SD -> QUICK.SNA";
    labels[ACT_SD_SNAP_LOAD] = "Charger SD  <- QUICK.SNA";
    labels[ACT_MONITOR]      = "Ouvrir Mini-Moniteur (F3)";
    labels[ACT_RESET_ORIC]   = "Reinitialiser l'Oric (F2)";

    for (uint8_t i = 0; i < TAB_FILES_COUNT; i++) {
        bool sel = (s_selRow[TAB_FILES] == i);
        snprintf(line, sizeof(line), "%c %-36.36s", sel ? '>' : ' ', labels[i]);
        osd_set_line((uint8_t)(CFG_ROW_BODY0 + 3u + i),
                     sel ? 4u : 0u,
                     sel ? 3u : 7u,
                     line);
    }
}

static void draw_tab_settings(void)
{
    OricConfig *cfg = config_get();
    static const char *const kPalNames[4]      = {"RGB Couleur", "Vert CRT", "Ambre CRT", "Noir&Blanc"};
    static const char *const kNesNames[5]      = {"Perso (PAD)", "Fleches+Esp", "ZQSD+Esp", "WASD+Esp", "IJKL+Ret"};
    static const char *const kAudioOutNames[3] = {"HDMI 48kHz", "I2S PCM5102", "PWM GPIO26"};

    char rows[TAB_SETTINGS_COUNT][40];
    snprintf(rows[OPT_MACHINE],    sizeof(rows[0]), "Mode Machine  : <%s>",
             cfg->microdisc_mode ? "Atmos Microdisc" : "Atmos Simple (TAP)");
    snprintf(rows[OPT_AUDIO_OUT],  sizeof(rows[0]), "Sortie Audio  : <%s>",
             kAudioOutNames[cfg->audio_out_mode % 3u]);
    snprintf(rows[OPT_AZERTY],     sizeof(rows[0]), "Clavier       : <%s>", cfg->azerty_kbd ? "AZERTY FR" : "QWERTY US");
    snprintf(rows[OPT_PALETTE],    sizeof(rows[0]), "Palette Video : <%s>", kPalNames[cfg->video_palette & 3u]);
    snprintf(rows[OPT_AUDIO_MUTE], sizeof(rows[0]), "Etat Audio    : <%s>", cfg->audio_mute ? "MUET" : "ACTIF");
    snprintf(rows[OPT_AUDIO_VOL],  sizeof(rows[0]), "Volume Audio  : <%u%%>", (unsigned)cfg->audio_volume);
    snprintf(rows[OPT_AUDIO_DC],   sizeof(rows[0]), "Filtre DC PSG : <%s>", cfg->audio_dc_filter ? "ON" : "OFF");
    snprintf(rows[OPT_NES_PRESET], sizeof(rows[0]), "Manette NES   : <%s>", kNesNames[cfg->nes_preset % 5u]);
    snprintf(rows[OPT_SAVE_CFG],   sizeof(rows[0]), "Sauver config -> SD (/oric.cfg)");
    snprintf(rows[OPT_DEF_CFG],    sizeof(rows[0]), "Valeurs par defaut");

    char line[40];
    for (uint8_t i = 0; i < TAB_SETTINGS_COUNT; i++) {
        bool sel = (s_selRow[TAB_SETTINGS] == i);
        snprintf(line, sizeof(line), "%c %-36.36s", sel ? '>' : ' ', rows[i]);
        osd_set_line((uint8_t)(CFG_ROW_BODY0 + 1u + i * 2u),
                     sel ? 4u : 0u,
                     sel ? 3u : 7u,
                     line);
    }
}

static const char *flash_qe_label(uint8_t code)
{
    switch (code) {
        case 0u: return "00h N/A";
        case 1u: return "01h Deja ON";
        case 2u: return "02h Vol OK";
        case 3u: return "03h NV31 OK";
        case 4u: return "04h ECHEC!";
        case 5u: return "05h NV01 OK";
        case 6u: return "06h ERR WIN";
        default: return "?";
    }
}

static void draw_tab_hardware(void)
{
    BoardHwInfo hw;
    board_hw_get_info(&hw);
    if (s_i2cCount < 0) {
        osd_scan_i2c();
    }

    char line[40];
    snprintf(line, sizeof(line), "CPU     : RP2350%c @ %u MHz",
             hw.is_rp2350b ? 'B' : 'A',
             (unsigned)(hw.sys_clock_hz / 1000000u));
    osd_set_line(CFG_ROW_BODY0 + 0u, 0u, 7u, line);

    snprintf(line, sizeof(line), "Flash   : %u Mo  ID=%02X-%02X-%02X-%02X",
             (unsigned)(hw.flash_size_bytes >> 20),
             hw.flash_jedec[0], hw.flash_jedec[1], hw.flash_jedec[2], hw.flash_jedec[3]);
    osd_set_line(CFG_ROW_BODY0 + 2u, 0u, 7u, line);

    snprintf(line, sizeof(line), "Puya QE : %s", flash_qe_label(hw.flash_qe_code));
    osd_set_line(CFG_ROW_BODY0 + 3u, 0u, (hw.flash_qe_code == 4u || hw.flash_qe_code == 6u) ? 1u : 2u, line);

    snprintf(line, sizeof(line), "QMI M0  : %08lX (Flash)", (unsigned long)hw.qmi_m0_timing);
    osd_set_line(CFG_ROW_BODY0 + 4u, 0u, 6u, line);

    if (hw.psram_size_bytes > 0u) {
        snprintf(line, sizeof(line), "PSRAM   : %u Mo GP%u ID=%02X/%02X [%s]",
                 (unsigned)(hw.psram_size_bytes >> 20),
                 (unsigned)hw.psram_cs_pin,
                 hw.psram_mfid, hw.psram_kgd,
                 hw.psram_test_ok ? "OK" : "ERR");
        osd_set_line(CFG_ROW_BODY0 + 6u, 0u, hw.psram_test_ok ? 2u : 1u, line);
    } else {
        snprintf(line, sizeof(line), "PSRAM   : Non detectee (ID=%02X/%02X)",
                 hw.psram_mfid, hw.psram_kgd);
        osd_set_line(CFG_ROW_BODY0 + 6u, 0u, 1u, line);
    }

    snprintf(line, sizeof(line), "QMI M1  : %08lX (PSRAM)", (unsigned long)hw.qmi_m1_timing);
    osd_set_line(CFG_ROW_BODY0 + 7u, 0u, 6u, line);

    snprintf(line, sizeof(line), "Carte SD: %s", hal_sd_is_ready() ? "PRETE (SPI1)" : "ABSENTE / ERR");
    osd_set_line(CFG_ROW_BODY0 + 9u, 0u, hal_sd_is_ready() ? 2u : 1u, line);

    int pos = snprintf(line, sizeof(line), "I2C1    : %d (", s_i2cCount);
    for (int i = 0; i < s_i2cCount && i < 6 && pos < 34; i++) {
        pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "%s%02X", i ? "," : "", s_i2cFound[i]);
    }
    snprintf(line + pos, sizeof(line) - (size_t)pos, ") [R=Scan]");
    osd_set_line(CFG_ROW_BODY0 + 11u, 0u, 7u, line);

    uint8_t nes = nes_read_blocking();
    snprintf(line, sizeof(line), "NES Live: %c %c %c %c  %s %s %c %c",
             (nes & 0x08u) ? '.' : 'H',
             (nes & 0x04u) ? '.' : 'B',
             (nes & 0x02u) ? '.' : 'G',
             (nes & 0x01u) ? '.' : 'D',
             (nes & 0x20u) ? "..." : "SEL",
             (nes & 0x10u) ? "..." : "STR",
             (nes & 0x40u) ? '.' : 'B',
             (nes & 0x80u) ? '.' : 'A');
    osd_set_line(CFG_ROW_BODY0 + 13u, 0u, 3u, line);
}

static void draw_tab_usb(void)
{
    UsbKbdStats st;
    usb_kbd_get_stats(&st);

    char line[40];
    snprintf(line, sizeof(line), "rhport0 (USB Natif)  : %s", st.rh0_active ? "ACTIF" : "INACTIF");
    osd_set_line(CFG_ROW_BODY0 + 0u, 0u, st.rh0_active ? 2u : 1u, line);

    snprintf(line, sizeof(line), "rhport1 (PIO GP28/29): %s", st.rh1_active ? "ACTIF" : "INACTIF");
    osd_set_line(CFG_ROW_BODY0 + 1u, 0u, st.rh1_active ? 2u : 1u, line);

    snprintf(line, sizeof(line), "TX_TO:%lu  Asserts:%lu  EnumErr:%lu",
             (unsigned long)st.tx_timeouts,
             (unsigned long)st.tusb_asserts,
             (unsigned long)st.enum_fails);
    osd_set_line(CFG_ROW_BODY0 + 3u, 0u, 6u, line);

    snprintf(line, sizeof(line), "ReArm:%lu  Desync:%lu  Montes:%u",
             (unsigned long)st.rearm_count,
             (unsigned long)st.desync_count,
             (unsigned)st.mounted_count);
    osd_set_line(CFG_ROW_BODY0 + 4u, 0u, 6u, line);

    osd_set_line(CFG_ROW_BODY0 + 6u, 0u, 3u, "PERIPHERIQUES HID CONNECTES:");
    uint8_t row = CFG_ROW_BODY0 + 7u;
    int shown = 0;
    for (size_t i = 0; i < USB_KBD_MAX_SLOTS && row < CFG_ROW_STATUS - 3u; i++) {
        if (!st.devs[i].mounted) {
            continue;
        }
        const char *port = (st.devs[i].rhport == 0u) ? "USB" : "PIO";
        const char *kind = st.devs[i].is_keyboard ? "KBD" : "HID";
        const char *prot = (st.devs[i].itf_protocol == 1u) ? "Boot" : "Rpt";
        snprintf(line, sizeof(line), " %s %04X:%04X %s %s Rx:%lu",
                 port,
                 (unsigned)st.devs[i].vid,
                 (unsigned)st.devs[i].pid,
                 kind, prot,
                 (unsigned long)st.devs[i].report_total);
        osd_set_line(row++, 0u, 7u, line);
        shown++;
    }
    if (shown == 0) {
        osd_set_line(row, 0u, 1u, "  Aucun peripherique HID detecte");
    }

    if (s_lastKeySc != 0u) {
        snprintf(line, sizeof(line), "Derniere touche: SC=%02X Ext=%u [%s]",
                 s_lastKeySc, s_lastKeyExt ? 1u : 0u, s_lastKeySrc ? "USB" : "PS2");
    } else {
        snprintf(line, sizeof(line), "Appuyez sur une touche pour tester...");
    }
    osd_set_line(CFG_ROW_STATUS - 2u, 0u, 3u, line);
}

static const uint8_t s_padToNesIdx[8] = {3u, 2u, 1u, 0u, 7u, 6u, 5u, 4u};
static const char *const s_padBtnLabels[8] = {
    "Haut (UP)    ",
    "Bas (DOWN)   ",
    "Gauche (LEFT)",
    "Droite(RIGHT)",
    "Bouton A     ",
    "Bouton B     ",
    "Select       ",
    "Start        "
};

static void draw_tab_gamepad(void)
{
    static const char *const kNesNames[5] = {"Perso", "Fleches+Esp", "ZQSD+Esp", "WASD+Esp", "IJKL+Ret"};

    osd_set_line(CFG_ROW_BODY0, 0u, 3u, "MAPPING MANETTE NES -> CLAVIER ORIC:");

    char line[40];
    for (uint8_t r = 0u; r < 8u; r++) {
        uint8_t nes_idx = s_padToNesIdx[r];
        uint8_t col_idx = 255u, row_idx = 255u;
        oric_keyboard_get_nes_key(nes_idx, &col_idx, &row_idx);
        bool sel = (s_selRow[TAB_GAMEPAD] == r);

        if (sel && s_padCapture) {
            snprintf(line, sizeof(line), "  %s : <APPUYEZ TOUCHE>", s_padBtnLabels[r]);
            osd_set_line((uint8_t)(CFG_ROW_BODY0 + 2u + r), 1u, 3u, line);
        } else {
            snprintf(line, sizeof(line), "  %s : <%s>",
                     s_padBtnLabels[r], oric_keyboard_key_name(col_idx, row_idx));
            osd_set_line((uint8_t)(CFG_ROW_BODY0 + 2u + r),
                         sel ? 4u : 0u,
                         sel ? 3u : 7u,
                         line);
        }
    }

    uint8_t preset = oric_keyboard_get_nes_preset();
    bool sel_pre = (s_selRow[TAB_GAMEPAD] == PAD_ROW_PRESET);
    snprintf(line, sizeof(line), "  Preset rapide : <%s>", kNesNames[preset % 5u]);
    osd_set_line((uint8_t)(CFG_ROW_BODY0 + 11u), sel_pre ? 4u : 0u, sel_pre ? 3u : 6u, line);

    bool sel_sav = (s_selRow[TAB_GAMEPAD] == PAD_ROW_SAVE);
    osd_set_line((uint8_t)(CFG_ROW_BODY0 + 13u), sel_sav ? 4u : 0u, sel_sav ? 3u : 2u,
                 "  Sauver config sur SD (/oric.cfg)");

    osd_set_line((uint8_t)(CFG_ROW_BODY0 + 16u), 0u, 6u,
                 "G/D: choisir touche dans la liste");
    osd_set_line((uint8_t)(CFG_ROW_BODY0 + 17u), 0u, 6u,
                 "ENTREE: appuyer sur touche clavier");
}

static void osd_render(void)
{
    osd_draw_chrome();
    switch (s_curTab) {
        case TAB_FILES:    draw_tab_files();    break;
        case TAB_SETTINGS: draw_tab_settings(); break;
        case TAB_HARDWARE: draw_tab_hardware(); break;
        case TAB_USB:      draw_tab_usb();      break;
        case TAB_GAMEPAD:  draw_tab_gamepad();  break;
        default:           break;
    }
}

static void adjust_setting(int delta)
{
    OricConfig *cfg = config_get();
    uint8_t sel = s_selRow[TAB_SETTINGS];
    bool mode_changed = false;

    switch (sel) {
        case OPT_MACHINE:
            cfg->microdisc_mode = !cfg->microdisc_mode;
            mode_changed = true;
            break;
        case OPT_AUDIO_OUT:
            cfg->audio_out_mode = (uint8_t)((cfg->audio_out_mode + 3 + delta) % 3);
            break;
        case OPT_AZERTY:
            cfg->azerty_kbd = !cfg->azerty_kbd;
            break;
        case OPT_PALETTE:
            cfg->video_palette = (uint8_t)((cfg->video_palette + 4 + delta) % 4);
            break;
        case OPT_AUDIO_MUTE:
            cfg->audio_mute = !cfg->audio_mute;
            break;
        case OPT_AUDIO_VOL: {
            static const uint8_t kVols[4] = {25u, 50u, 75u, 100u};
            int idx = 3;
            for (int i = 0; i < 4; i++) {
                if (cfg->audio_volume == kVols[i]) {
                    idx = i;
                    break;
                }
            }
            idx = (idx + 4 + delta) % 4;
            cfg->audio_volume = kVols[idx];
            break;
        }
        case OPT_AUDIO_DC:
            cfg->audio_dc_filter = !cfg->audio_dc_filter;
            break;
        case OPT_NES_PRESET:
            cfg->nes_preset = (uint8_t)((cfg->nes_preset + 5 + delta) % 5);
            break;
        default:
            return;
    }

    config_apply(cfg);
    if (mode_changed) {
        busResetMachine();
        osd_status(true, cfg->microdisc_mode ? "MODE MICRODISC + RESET OK"
                                             : "MODE ATMOS SIMPLE + RESET OK");
    } else {
        osd_status(true, "REGLAGE APPLIQUE (NON SAUVE SD)");
    }
}

static void move_selection(int dir)
{
    if (s_browserMode != BROWSER_NONE && s_tapCount > 0) {
        s_tapSel = (s_tapSel + s_tapCount + dir) % s_tapCount;
        return;
    }
    if (s_curTab == TAB_FILES) {
        s_selRow[TAB_FILES] = (uint8_t)((s_selRow[TAB_FILES] + TAB_FILES_COUNT + dir) % TAB_FILES_COUNT);
    } else if (s_curTab == TAB_SETTINGS) {
        s_selRow[TAB_SETTINGS] = (uint8_t)((s_selRow[TAB_SETTINGS] + TAB_SETTINGS_COUNT + dir) % TAB_SETTINGS_COUNT);
    } else if (s_curTab == TAB_GAMEPAD) {
        s_selRow[TAB_GAMEPAD] = (uint8_t)((s_selRow[TAB_GAMEPAD] + TAB_GAMEPAD_COUNT + dir) % TAB_GAMEPAD_COUNT);
    }
}

static void adjust_horz(int dir)
{
    if (s_browserMode != BROWSER_NONE) {
        return;
    }
    if (s_curTab == TAB_SETTINGS) {
        adjust_setting(dir);
    } else if (s_curTab == TAB_GAMEPAD) {
        uint8_t row = s_selRow[TAB_GAMEPAD];
        if (row < 8u) {
            oric_keyboard_cycle_nes_key(s_padToNesIdx[row], dir);
            OricConfig *cfg = config_get();
            cfg->nes_preset = 0u;
            osd_status(true, "TOUCHE MANETTE MODIFIEE (PERSO)");
        } else if (row == PAD_ROW_PRESET) {
            OricConfig *cfg = config_get();
            cfg->nes_preset = (uint8_t)((cfg->nes_preset + 5 + dir) % 5);
            oric_keyboard_set_nes_preset(cfg->nes_preset);
            osd_status(true, "PRESET MANETTE APPLIQUE");
        }
    } else if (s_curTab == TAB_FILES &&
               (s_selRow[TAB_FILES] == ACT_PSRAM_SAVE || s_selRow[TAB_FILES] == ACT_PSRAM_LOAD)) {
        s_psramSlot = (uint8_t)((s_psramSlot + 4 + dir) & 3);
    } else {
        s_curTab = (uint8_t)((s_curTab + CFG_TAB_COUNT + dir) % CFG_TAB_COUNT);
    }
}

/* Retourne false s'il faut quitter le configurateur */
static bool activate_row(void)
{
    if (s_curTab == TAB_FILES) {
        if (s_browserMode != BROWSER_NONE) {
            if (s_tapCount > 0 && s_tapSel >= 0 && s_tapSel < s_tapCount) {
                char (*list)[SD_MAX_FILENAME_LEN] = tape_shared_file_list();
                if (s_browserMode == BROWSER_DSK) {
                    if (microdisc_insert_disk(0u, list[s_tapSel])) {
                        s_browserMode = BROWSER_NONE;
                        OricConfig *cfg = config_get();
                        if (!cfg->microdisc_mode) {
                            cfg->microdisc_mode = true;
                            config_apply(cfg);
                            busResetMachine();
                            osd_status(true, ".DSK MONTEE + BOOT MICRODISC");
                        } else {
                            osd_status(true, ".DSK MONTEE EN A: (ESC:RETOUR)");
                        }
                    } else {
                        osd_status(false, "ERR: OUVERTURE/FORMAT .DSK");
                    }
                } else {
                    if (tape_insert_file(list[s_tapSel])) {
                        s_browserMode = BROWSER_NONE;
                        osd_status(true, "CASSETTE .TAP MONTEE");
                    } else {
                        osd_status(false, "ERR: OUVERTURE .TAP");
                    }
                }
            } else {
                s_browserMode = BROWSER_NONE;
            }
            return true;
        }

        switch (s_selRow[TAB_FILES]) {
            case ACT_PICK_DSK: {
                if (!microdisc_storage_ready()) {
                    osd_status(false, "ERR: PSRAM REQUISE POUR .DSK");
                    return true;
                }
                char (*list)[SD_MAX_FILENAME_LEN] = tape_shared_file_list();
                s_tapCount    = hal_sd_list_files(".dsk", list, SD_MAX_LIST_FILES, false);
                s_tapSel      = 0;
                s_tapScroll   = 0;
                s_browserMode = BROWSER_DSK;
                osd_status(s_tapCount >= 0, (s_tapCount > 0) ? "CHOISIR .DSK + ENTREE" : "AUCUN .DSK");
                return true;
            }
            case ACT_SAVE_DSK:
                if (!microdisc_has_disk(0u)) {
                    osd_status(false, "AUCUNE DISQUETTE EN A:");
                } else if (microdisc_save_disk(0u)) {
                    osd_status(true, "OK: .DSK SAUVEE SUR SD");
                } else {
                    osd_status(false, "ERR: SAUVEGARDE .DSK");
                }
                return true;
            case ACT_EJECT_DSK:
                if (!microdisc_has_disk(0u)) {
                    osd_status(false, "AUCUNE DISQUETTE EN A:");
                } else {
                    microdisc_eject_disk(0u, true);
                    osd_status(true, "DISQUETTE A: EJECTEE");
                }
                return true;
            case ACT_BOOT_DSK: {
                OricConfig *cfg = config_get();
                if (!cfg->microdisc_mode) {
                    cfg->microdisc_mode = true;
                    config_apply(cfg);
                }
                busResetMachine();
                return false;
            }
            case ACT_PICK_TAP: {
                char (*list)[SD_MAX_FILENAME_LEN] = tape_shared_file_list();
                s_tapCount    = hal_sd_list_files(".tap", list, SD_MAX_LIST_FILES, true);
                s_tapSel      = 0;
                s_tapScroll   = 0;
                s_browserMode = BROWSER_TAP;
                osd_status(s_tapCount >= 0, (s_tapCount > 0) ? "CHOISIR .TAP + ENTREE" : "AUCUN .TAP");
                return true;
            }
            case ACT_EJECT_TAP:
                tape_reset();
                osd_status(true, "CASSETTE EJECTEE");
                return true;
            case ACT_AUTOCLOAD:
                if (!tape_is_active()) {
                    osd_status(false, "INSEREZ D'ABORD UN .TAP");
                    return true;
                }
                busAutoType("CLOAD\"\"\r", 0u);
                return false;
            case ACT_RESET_CLOAD: {
                if (!tape_is_active()) {
                    osd_status(false, "INSEREZ D'ABORD UN .TAP");
                    return true;
                }
                char keep_tap[SD_MAX_FILENAME_LEN];
                snprintf(keep_tap, sizeof(keep_tap), "%s", tape_current_name());
                busResetMachine();
                tape_insert_file(keep_tap);
                busAutoType("CLOAD\"\"\r", 110u);
                return false;
            }
            case ACT_BMP:
                if (monitor_save_bmp("CAPTURE")) {
                    osd_status(true, "OK: CAPTURE.BMP SAUVEE");
                } else {
                    osd_status(false, "ERR: SAUVEGARDE BMP");
                }
                return true;
            case ACT_PSRAM_SAVE:
                if (monitor_save_snap_psram(s_psramSlot)) {
                    osd_status(true, "OK: SNAP SAUVE EN PSRAM");
                } else {
                    osd_status(false, "ERR: PSRAM INDISPONIBLE");
                }
                return true;
            case ACT_PSRAM_LOAD:
                if (monitor_load_snap_psram(s_psramSlot)) {
                    osd_status(true, "OK: SNAP CHARGE DEPUIS PSRAM");
                    return false;
                }
                osd_status(false, "ERR: SLOT PSRAM VIDE/INVALIDE");
                return true;
            case ACT_SD_SNAP_SAVE:
                if (monitor_save_snap("QUICK")) {
                    osd_status(true, "OK: QUICK.SNA SAUVE SUR SD");
                } else {
                    osd_status(false, "ERR: SAUVEGARDE QUICK.SNA");
                }
                return true;
            case ACT_SD_SNAP_LOAD:
                if (monitor_load_snap("QUICK")) {
                    osd_status(true, "OK: QUICK.SNA CHARGE");
                    return false;
                }
                osd_status(false, "ERR: CHARGEMENT QUICK.SNA");
                return true;
            case ACT_MONITOR:
                monitor_run();
                return false;
            case ACT_RESET_ORIC:
                busResetMachine();
                return false;
            default:
                return true;
        }
    }

    if (s_curTab == TAB_SETTINGS) {
        if (s_selRow[TAB_SETTINGS] == OPT_SAVE_CFG) {
            if (config_save()) {
                osd_status(true, "OK: SAUVE /oric.cfg SUR SD");
            } else {
                osd_status(false, "ERR: ECRITURE /oric.cfg");
            }
            return true;
        }
        if (s_selRow[TAB_SETTINGS] == OPT_DEF_CFG) {
            OricConfig *cfg = config_get();
            bool prev_md = cfg->microdisc_mode;
            config_set_defaults(cfg);
            config_apply(cfg);
            if (prev_md != cfg->microdisc_mode) {
                busResetMachine();
            }
            osd_status(true, "REGLAGES PAR DEFAUT APPLIQUES");
            return true;
        }
        adjust_setting(1);
        return true;
    }

    if (s_curTab == TAB_HARDWARE) {
        osd_scan_i2c();
        board_psram_quick_test();
        osd_status(true, "SCAN I2C & TEST PSRAM EXECUTES");
        return true;
    }

    if (s_curTab == TAB_GAMEPAD) {
        uint8_t row = s_selRow[TAB_GAMEPAD];
        if (row < 8u) {
            s_padCapture = true;
            osd_status(true, "APPUYEZ SUR LA TOUCHE ORIC DESIREE");
            return true;
        }
        if (row == PAD_ROW_PRESET) {
            adjust_horz(1);
            return true;
        }
        if (row == PAD_ROW_SAVE) {
            if (config_save()) {
                osd_status(true, "OK: MAPPING + CONFIG SAUVES (/oric.cfg)");
            } else {
                osd_status(false, "ERR: ECRITURE /oric.cfg");
            }
            return true;
        }
    }

    return true;
}

void config_menu_run(void)
{
    s_browserMode = BROWSER_NONE;
    s_padCapture  = false;
    s_i2cCount    = -1;
    s_lastKeySc   = 0u;
    osd_status(true, "PRET - ENTREE:OK | TAB:ONGLET | ESC:FIN");

    monitorEnable(true);
    osd_render();

    input_kbd_clear();
    oric_keyboard_clear();

    /* Attendre le relachement initial de Select+Start NES si entree via manette */
    while ((nes_read_blocking() & 0x30u) == 0u) {
        usb_kbd_poll();
        sleep_ms(5);
    }

    uint8_t prev_nes      = 0xFFu;
    uint32_t last_refresh = (uint32_t)(time_us_64() / 1000u);
    uint32_t nes_hold_ms  = 0u;
    uint8_t  nes_hold_btn = 0u;

    bool running = true;
    while (running) {
        bool dirty = false;
        uint32_t now = (uint32_t)(time_us_64() / 1000u);

        Ps2KeyEvent ev;
        while (input_kbd_poll(&ev)) {
            if (!ev.pressed) {
                continue;
            }
            s_lastKeySc  = ev.code;
            s_lastKeyExt = ev.extended;
            s_lastKeySrc = ev.source;
            if (s_curTab == TAB_USB) {
                dirty = true;
            }

            if (s_padCapture) {
                s_padCapture = false;
                if (ev.code == 0x76u && !ev.extended) { /* ESC annule la capture */
                    osd_status(true, "CAPTURE ANNULEE");
                } else {
                    uint8_t col_idx = 255u, row_idx = 255u;
                    if (oric_keyboard_scancode_to_matrix(ev.code, ev.extended, &col_idx, &row_idx)) {
                        uint8_t row = s_selRow[TAB_GAMEPAD];
                        if (row < 8u) {
                            oric_keyboard_set_nes_key(s_padToNesIdx[row], col_idx, row_idx);
                            config_get()->nes_preset = 0u;
                            char msg[39];
                            snprintf(msg, sizeof(msg), "AFFECTE: %s", oric_keyboard_key_name(col_idx, row_idx));
                            osd_status(true, msg);
                        }
                    } else {
                        osd_status(false, "TOUCHE NON MAPPABLE SUR ORIC");
                    }
                }
                dirty = true;
                continue;
            }

            if (ev.code == 0x07u || ev.code == 0x76u || ev.code == 0x0Cu || ev.code == 0x78u) {
                /* F12 / ESC / F4 / F11 */
                if (s_browserMode != BROWSER_NONE && ev.code == 0x76u) {
                    s_browserMode = BROWSER_NONE;
                    dirty = true;
                } else {
                    running = false;
                }
                break;
            }

            if (ev.code == 0x0Du && !ev.extended) { /* Tab */
                s_browserMode = BROWSER_NONE;
                s_curTab = (uint8_t)((s_curTab + 1u) % CFG_TAB_COUNT);
                dirty = true;
                continue;
            }

            if (!ev.extended && (ev.code == 0x16u || ev.code == 0x1Eu || ev.code == 0x26u || ev.code == 0x25u || ev.code == 0x2Eu)) {
                /* 1..5 */
                s_browserMode = BROWSER_NONE;
                if (ev.code == 0x16u) s_curTab = 0u;
                else if (ev.code == 0x1Eu) s_curTab = 1u;
                else if (ev.code == 0x26u) s_curTab = 2u;
                else if (ev.code == 0x25u) s_curTab = 3u;
                else s_curTab = 4u;
                dirty = true;
                continue;
            }

            if (ev.code == 0x75u && ev.extended) { /* Up */
                move_selection(-1);
                dirty = true;
                continue;
            }

            if (ev.code == 0x72u && ev.extended) { /* Down */
                move_selection(1);
                dirty = true;
                continue;
            }

            if (ev.code == 0x6Bu && ev.extended) { /* Left */
                adjust_horz(-1);
                dirty = true;
                continue;
            }

            if (ev.code == 0x74u && ev.extended) { /* Right */
                adjust_horz(1);
                dirty = true;
                continue;
            }

            if (ev.code == 0x5Au || ev.code == 0x29u) { /* Enter / Space */
                running = activate_row();
                dirty = true;
                if (!running) {
                    break;
                }
                continue;
            }

            if (ev.code == 0x2Du && !ev.extended && s_curTab == TAB_HARDWARE) { /* R */
                osd_scan_i2c();
                board_psram_quick_test();
                osd_status(true, "SCAN I2C & TEST PSRAM OK");
                dirty = true;
            }
        }

        if (!running) {
            break;
        }

        /* Navigation manette NES (Select = onglet, D-Pad = navig, A = Entree, B = Retour) */
        uint8_t nes = nes_read_blocking();
        uint8_t edge = (uint8_t)(prev_nes & (uint8_t)~nes);
        uint8_t held = (uint8_t)(~nes & 0x0Fu);
        if (held != 0u && held == nes_hold_btn) {
            if ((uint32_t)(now - nes_hold_ms) > 220u) {
                edge |= held;
                nes_hold_ms = now;
            }
        } else {
            nes_hold_btn = held;
            nes_hold_ms  = now;
        }
        prev_nes = nes;

        if (edge & 0x20u) { /* Select */
            s_browserMode = BROWSER_NONE;
            s_curTab = (uint8_t)((s_curTab + 1u) % CFG_TAB_COUNT);
            dirty = true;
        }
        if (edge & 0x40u) { /* B */
            if (s_browserMode != BROWSER_NONE) {
                s_browserMode = BROWSER_NONE;
                dirty = true;
            } else {
                running = false;
            }
        }
        if (edge & 0x80u) { /* A */
            running = activate_row();
            dirty = true;
        }
        if (edge & 0x08u) { /* Up */
            move_selection(-1);
            dirty = true;
        }
        if (edge & 0x04u) { /* Down */
            move_selection(1);
            dirty = true;
        }
        if (edge & 0x02u) { /* Left */
            if (s_browserMode == BROWSER_NONE) {
                adjust_horz(-1);
                dirty = true;
            }
        }
        if (edge & 0x01u) { /* Right */
            if (s_browserMode == BROWSER_NONE) {
                adjust_horz(1);
                dirty = true;
            }
        }

        if ((s_curTab == TAB_HARDWARE || s_curTab == TAB_USB) &&
            (uint32_t)(now - last_refresh) >= 200u) {
            last_refresh = now;
            dirty = true;
        }

        if (dirty && running) {
            osd_render();
        }
        sleep_ms(5);
    }

    input_kbd_clear();
    oric_keyboard_clear();
    monitorEnable(false);
}
