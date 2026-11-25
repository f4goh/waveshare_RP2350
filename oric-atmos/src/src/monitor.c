/*
 * Mini-moniteur et debogueur integre Oric Atmos
 * Commandes :
 *   H / ?             Aide
 *   M <addr>          Dump memoire (25 lignes, Entree pour continuer)
 *   D <addr>          Desassemblage 6502 + I/O Oric (Entree pour continuer)
 *   R                 Registres CPU 6502, VIA 6522, ACIA et cassette
 *   S                 Pas a pas (1 instruction CPU + registres + desasm)
 *   G [addr]          Continuer l'execution (optionnellement a <addr>)
 *   MM <addr> <v...>  Modifier 1..N octets en RAM
 *   F <addr> <len> <v> Remplir une zone RAM
 *   FW <addr> <len> <f> Sauver bloc RAM dans <f>.bin
 *   FR <addr> <f>     Charger <f>.bin en RAM a <addr>
 *   SNS <f> / SNL <f> Sauver / charger snapshot <f>.sna
 *   BMP <f>           Capture d'ecran Oric dans <f>.bmp
 *   I2C               Scan du bus I2C1
 *   DIR               Lister les fichiers sur la carte SD
 *   TAPE              Ejecter le fichier .tap en cours
 *   RESET             Reinitialiser l'Oric
 *   Q / X / F12 / ESC Quitter le moniteur
 */

#include "monitor.h"
#include "acia6551.h"
#include "arduino_hal.h"
#include "microdisc.h"
#include "audio.h"
#include "board_hw.h"
#include "bus.h"
#include "config.h"
#include "input_kbd.h"
#include "oric_keyboard.h"
#include "ps2_kbd.h"
#include "tape.h"
#include "ula.h"
#include "via6522.h"
#include "video.h"
#include "vrEmu6502.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MON_CMD_MAX    34U
#define MON_ROW_STATUS 26U
#define MON_ROW_PROMPT 27U
#define MON_BODY_ROWS  25U

typedef enum {
    REPEAT_NONE = 0,
    REPEAT_DUMP,
    REPEAT_DISASM,
    REPEAT_STEP
} RepeatMode;

typedef struct {
    char    buf[MON_CMD_MAX];
    uint8_t len;
    uint8_t cursor;
} CmdLine;

static uint16_t   s_dumpAddr   = 0xBB80u;
static uint16_t   s_disasmAddr = 0x0600u;
static RepeatMode s_repeatMode = REPEAT_NONE;

/* --------------------------------------------------------------------------
 * Utilitaires d'affichage dans ramMoni (40x28)
 * -------------------------------------------------------------------------- */
static void mon_clear_screen(void)
{
    memset(ramMoni, ' ', ORIC_MONITOR_SIZE);
    for (uint8_t y = 0; y < ORIC_SCREEN_ROWS; y++) {
        ramMoni[y * ORIC_SCREEN_COLS + 0] = 0x14u; /* PAPER blue */
        ramMoni[y * ORIC_SCREEN_COLS + 1] = 0x07u; /* INK white */
    }
    ramMoni[0] = 0x14u;
    ramMoni[1] = 0x03u; /* Titre jaune */

    char hdr[38];
    snprintf(hdr, sizeof(hdr), "MONITEUR PC=%04X A=%02X X=%02X Y=%02X SP=%02X",
             vrEmu6502GetPC(g_cpu),
             vrEmu6502GetAcc(g_cpu),
             vrEmu6502GetX(g_cpu),
             vrEmu6502GetY(g_cpu),
             vrEmu6502GetStackPointer(g_cpu));
    for (size_t i = 0; hdr[i] && i < 38u; i++) {
        ramMoni[2u + i] = (uint8_t)hdr[i];
    }

    ramMoni[MON_ROW_PROMPT * ORIC_SCREEN_COLS + 0] = 0x10u;
    ramMoni[MON_ROW_PROMPT * ORIC_SCREEN_COLS + 1] = 0x02u; /* Vert */
    ramMoni[MON_ROW_PROMPT * ORIC_SCREEN_COLS + 2] = '>';
}

static void mon_clear_body(void)
{
    for (uint8_t row = 1; row <= MON_BODY_ROWS; row++) {
        uint8_t *line = &ramMoni[row * ORIC_SCREEN_COLS];
        line[0] = 0x14u;
        line[1] = 0x07u;
        memset(&line[2], ' ', ORIC_SCREEN_COLS - 2u);
    }
}

static void mon_put_line(uint8_t row, const char *txt)
{
    if (row >= ORIC_SCREEN_ROWS) {
        return;
    }
    uint8_t *dst = &ramMoni[row * ORIC_SCREEN_COLS + 2u];
    memset(dst, ' ', ORIC_SCREEN_COLS - 2u);
    for (size_t i = 0; txt[i] && i < (ORIC_SCREEN_COLS - 2u); i++) {
        uint8_t ch = (uint8_t)txt[i];
        dst[i] = (ch >= 32u && ch < 127u) ? ch : '.';
    }
}

static void mon_status(bool ok, const char *msg)
{
    ramMoni[MON_ROW_STATUS * ORIC_SCREEN_COLS + 0] = 0x10u;
    ramMoni[MON_ROW_STATUS * ORIC_SCREEN_COLS + 1] = ok ? 0x06u : 0x01u; /* Cyan ou Rouge */
    mon_put_line(MON_ROW_STATUS, msg);
}

static void mon_draw_prompt(const CmdLine *c)
{
    uint8_t *dst = &ramMoni[MON_ROW_PROMPT * ORIC_SCREEN_COLS + 3u];
    memset(dst, ' ', ORIC_SCREEN_COLS - 3u);
    for (uint8_t i = 0; i < c->len && i < (ORIC_SCREEN_COLS - 4u); i++) {
        dst[i] = (uint8_t)c->buf[i];
    }
    if (c->cursor < (ORIC_SCREEN_COLS - 3u)) {
        dst[c->cursor] = 127u; /* Curseur bloc */
    }
}

/* --------------------------------------------------------------------------
 * Parsing hexadecimal souple (espaces optionnels, 1..4 chiffres)
 * -------------------------------------------------------------------------- */
static const char *skip_ws(const char *s)
{
    while (*s == ' ') s++;
    return s;
}

static bool parse_hex_u16(const char **p, uint16_t *out)
{
    const char *s = skip_ws(*p);
    if (*s == '$' || *s == '#') {
        s++;
    }
    if (!isxdigit((unsigned char)*s)) {
        return false;
    }
    uint32_t v = 0;
    int digits = 0;
    while (isxdigit((unsigned char)*s) && digits < 4) {
        char c = (char)toupper((unsigned char)*s++);
        v = (v << 4) | (uint32_t)(c <= '9' ? (c - '0') : (c - 'A' + 10));
        digits++;
    }
    *out = (uint16_t)v;
    *p   = s;
    return true;
}

static bool parse_hex_u8(const char **p, uint8_t *out)
{
    const char *s = skip_ws(*p);
    if (*s == '$' || *s == '#') {
        s++;
    }
    if (!isxdigit((unsigned char)*s)) {
        return false;
    }
    uint32_t v = 0;
    int digits = 0;
    while (isxdigit((unsigned char)*s) && digits < 2) {
        char c = (char)toupper((unsigned char)*s++);
        v = (v << 4) | (uint32_t)(c <= '9' ? (c - '0') : (c - 'A' + 10));
        digits++;
    }
    *out = (uint8_t)v;
    *p   = s;
    return true;
}

static bool parse_word(const char **p, char *dst, size_t max_len)
{
    const char *s = skip_ws(*p);
    if (!*s) {
        return false;
    }
    size_t i = 0;
    while (*s && *s != ' ' && i + 1u < max_len) {
        dst[i++] = *s++;
    }
    dst[i] = '\0';
    *p = s;
    return i > 0;
}

/* --------------------------------------------------------------------------
 * Commandes d'inspection
 * -------------------------------------------------------------------------- */
static void cmd_help(void)
{
    static const char *const lines[] = {
        "COMMANDES MONITEUR ORIC:",
        "H / ?          Cette aide",
        "M <addr>       Dump hex/ASCII (25 lig)",
        "D <addr>       Desassemblage 6502",
        "R              Registres CPU/VIA/TAP",
        "S              Pas a pas (1 instr)",
        "G [addr]       Reprendre execution",
        "MM <adr> <v..> Ecrire octets en RAM",
        "F <adr> <n> <v> Remplir zone RAM",
        "FW <adr> <n> <f> Sauver RAM -> <f>.BIN",
        "FR <adr> <f>   Charger <f>.BIN -> RAM",
        "SNS/SNL <f>    Sauver/charger <f>.SNA",
        "BMP <f>        Capture ecran -> <f>.BMP",
        "I2C            Scanner bus I2C1",
        "DIR            Lister fichiers SD",
        "TAPE           Ejecter fichier .TAP",
        "RESET          Reset Oric Atmos",
        "Q / X / F12    Quitter le moniteur",
        "(Entree seule repete M / D / S)"
    };
    mon_clear_body();
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]) && i < MON_BODY_ROWS; i++) {
        mon_put_line((uint8_t)(1u + i), lines[i]);
    }
    mon_status(true, "OK: AIDE");
}

static void cmd_dump_mem(void)
{
    mon_clear_body();
    uint16_t addr = s_dumpAddr;
    char line[40];

    for (uint8_t row = 0; row < MON_BODY_ROWS; row++) {
        int pos = snprintf(line, sizeof(line), "%04X ", addr);
        char ascii[9];
        for (int i = 0; i < 8; i++) {
            uint8_t v = busRead((uint16_t)(addr + i), true);
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "%02X ", v);
            ascii[i] = (v >= 32u && v <= 126u) ? (char)v : '.';
        }
        ascii[8] = '\0';
        snprintf(line + pos, sizeof(line) - (size_t)pos, "%s", ascii);
        mon_put_line((uint8_t)(1u + row), line);
        addr = (uint16_t)(addr + 8u);
    }
    s_dumpAddr = addr;
}

static const char *oric_io_tag(uint16_t addr)
{
    if (addr >= 0x031Cu && addr <= 0x031Fu) {
        static const char *const acia_names[4] = { "ACIA RX", "ACIA ST", "ACIA CM", "ACIA CT" };
        return acia_names[addr & 3u];
    }
    if (addr >= 0x0300u && addr <= 0x03FFu) {
        static const char *const via_names[16] = {
            "ORB", "ORA", "DDRB", "DDRA", "T1CL", "T1CH", "T1LL", "T1LH",
            "T2CL", "T2CH", "SR", "ACR", "PCR", "IFR", "IER", "ORA2"
        };
        return via_names[addr & 0x0Fu];
    }
    if (addr >= 0xA000u && addr <= 0xBFDFu) return "SCR";
    return NULL;
}

static void cmd_disasm_page(uint8_t start_row, uint8_t num_rows)
{
    uint16_t pc = s_disasmAddr;
    char mnemonic[36];
    char line[64];

    for (uint8_t r = 0; r < num_rows; r++) {
        uint16_t ref_addr = 0;
        uint16_t next_pc  = vrEmu6502DisassembleInstruction(
            g_cpu, pc, (int)sizeof(mnemonic), mnemonic, &ref_addr, NULL);

        uint16_t len = (uint16_t)(next_pc - pc);
        if (len == 0 || len > 3u) len = 1u;

        /* Format compact sur 38 colonnes: "XXXX BB BB BB  MNEM       ;TAG" */
        char bytes[10];
        if (len == 1u) {
            snprintf(bytes, sizeof(bytes), "%02X      ", busRead(pc, true));
        } else if (len == 2u) {
            snprintf(bytes, sizeof(bytes), "%02X %02X   ",
                     busRead(pc, true), busRead((uint16_t)(pc + 1u), true));
        } else {
            snprintf(bytes, sizeof(bytes), "%02X %02X %02X",
                     busRead(pc, true),
                     busRead((uint16_t)(pc + 1u), true),
                     busRead((uint16_t)(pc + 2u), true));
        }

        /* Met en majuscules le mnemonique */
        for (size_t i = 0; mnemonic[i]; i++) {
            mnemonic[i] = (char)toupper((unsigned char)mnemonic[i]);
        }

        const char *tag = (len == 3u) ? oric_io_tag(ref_addr) : NULL;
        if (tag) {
            snprintf(line, sizeof(line), "%04X %s %-14.14s;%s", pc, bytes, mnemonic, tag);
        } else {
            snprintf(line, sizeof(line), "%04X %s %s", pc, bytes, mnemonic);
        }
        mon_put_line((uint8_t)(start_row + r), line);
        pc = next_pc;
    }
    s_disasmAddr = pc;
}

static void cmd_show_regs(uint8_t start_row)
{
    uint8_t p = vrEmu6502GetStatus(g_cpu);
    char flags[9] = {
        (p & 0x80u) ? 'N' : '.',
        (p & 0x40u) ? 'V' : '.',
        '-',
        (p & 0x10u) ? 'B' : '.',
        (p & 0x08u) ? 'D' : '.',
        (p & 0x04u) ? 'I' : '.',
        (p & 0x02u) ? 'Z' : '.',
        (p & 0x01u) ? 'C' : '.',
        '\0'
    };

    char line[40];
    snprintf(line, sizeof(line), "PC:%04X A:%02X X:%02X Y:%02X SP:%02X",
             vrEmu6502GetPC(g_cpu), vrEmu6502GetAcc(g_cpu),
             vrEmu6502GetX(g_cpu), vrEmu6502GetY(g_cpu),
             vrEmu6502GetStackPointer(g_cpu));
    mon_put_line(start_row, line);

    snprintf(line, sizeof(line), "STATUS:%02X [%s] PATTR:%02X",
             p, flags, videoGetPattr());
    mon_put_line((uint8_t)(start_row + 1u), line);

    snprintf(line, sizeof(line), "VIA IFR:%02X IER:%02X PCR:%02X ACR:%02X",
             via_mon_read(&g_via, VIA_IFR),
             via_mon_read(&g_via, VIA_IER),
             via_mon_read(&g_via, VIA_PCR),
             via_mon_read(&g_via, VIA_ACR));
    mon_put_line((uint8_t)(start_row + 2u), line);

    snprintf(line, sizeof(line), "T1:%02X%02X T2:%02X%02X  ACIA ST:%02X",
             via_mon_read(&g_via, VIA_T1C_H), via_mon_read(&g_via, VIA_T1C_L),
             via_mon_read(&g_via, VIA_T2C_H), via_mon_read(&g_via, VIA_T2C_L),
             g_acia.regs[ACIA_STATUS]);
    mon_put_line((uint8_t)(start_row + 3u), line);

    if (microdisc_is_enabled()) {
        snprintf(line, sizeof(line), "FDC:D%u T%02u MD:%02X WD:%02X %s",
                 (unsigned)microdisc_current_drive(),
                 (unsigned)microdisc_current_track(),
                 (unsigned)microdisc_status_reg(),
                 (unsigned)microdisc_wd_status_reg(),
                 microdisc_has_disk(0u) ? (microdisc_is_modified(0u) ? "MOD" : "DSK") : "VID");
    } else {
        snprintf(line, sizeof(line), "TAPE:%s REM:%lu",
                 tape_is_active() ? "OPEN" : "IDLE",
                 (unsigned long)tape_bytes_remaining());
    }
    mon_put_line((uint8_t)(start_row + 4u), line);
}

/* --------------------------------------------------------------------------
 * Snapshot & Capture BMP
 * -------------------------------------------------------------------------- */
static uint32_t fnv1a_32(uint32_t h, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        h ^= (uint32_t)data[i];
        h *= 16777619u;
    }
    return h;
}

#define PSRAM_SNAP_BASE_OFF 0x100000UL
#define PSRAM_SNAP_STRIDE   65536UL
#define PSRAM_SNAP_SLOTS    4U

typedef struct {
    char     magic[4];
    uint16_t hdr_len;
    uint16_t video_mode;
    uint32_t checksum;
} PsramSnapPrefix;

static volatile uint8_t *psram_slot_ptr(uint8_t slot)
{
    if (slot >= PSRAM_SNAP_SLOTS || board_psram_size() < (PSRAM_SNAP_BASE_OFF + PSRAM_SNAP_SLOTS * PSRAM_SNAP_STRIDE)) {
        return NULL;
    }
    return (volatile uint8_t *)(BOARD_PSRAM_BASE + PSRAM_SNAP_BASE_OFF + (uint32_t)slot * PSRAM_SNAP_STRIDE);
}

static size_t build_snap_header(uint8_t hdr[256])
{
    uint8_t cpus   = cpuSize();
    uint8_t vias   = viaSize();
    uint8_t audios = audioSize();
    size_t  total  = (size_t)cpus + vias + audios + 12u;
    if (total > 256u) {
        return 0u;
    }

    uint8_t *p = hdr;
    memcpy(p, "CPU", 3); p += 3;
    vrEmu6502SaveState(g_cpu, p); p += cpus;
    memcpy(p, "VIA", 3); p += 3;
    viaSaveState(&g_via, p); p += vias;
    memcpy(p, "PSG", 3); p += 3;
    audioSaveState(p); p += audios;
    memcpy(p, "RAM", 3);
    return total;
}

static bool apply_snap_header(const uint8_t *hdr)
{
    if (memcmp(hdr, "CPU", 3) != 0) {
        return false;
    }
    uint8_t cpus   = cpuSize();
    uint8_t vias   = viaSize();
    const uint8_t *p = hdr + 3;
    vrEmu6502LoadState(g_cpu, p); p += cpus;
    p += 3;
    viaLoadState(&g_via, p); p += vias;
    p += 3;
    audioLoadState(p);
    videoSetMode(ram[0x021Fu]);
    return true;
}

static bool cmd_snap_save(const char *filename)
{
    uint8_t hdr[256];
    size_t  total = build_snap_header(hdr);
    if (total == 0u) {
        return false;
    }
    char path[48];
    snprintf(path, sizeof(path), "%s.sna", filename);
    return hal_sd_save_snap(path, hdr, total, ram, ORIC_RAM_SIZE) > 0;
}

static bool cmd_snap_load(const char *filename)
{
    uint8_t cpus   = cpuSize();
    uint8_t vias   = viaSize();
    uint8_t audios = audioSize();
    uint8_t hdr[256];
    size_t  total = (size_t)cpus + vias + audios + 12u;
    if (total > sizeof(hdr)) {
        return false;
    }

    char path[48];
    snprintf(path, sizeof(path), "%s.sna", filename);
    if (hal_sd_load_snap(path, hdr, total, ram, ORIC_RAM_SIZE) <= 0) {
        return false;
    }
    return apply_snap_header(hdr);
}

bool monitor_save_snap(const char *base_name)
{
    return (base_name && base_name[0]) ? cmd_snap_save(base_name) : false;
}

bool monitor_load_snap(const char *base_name)
{
    return (base_name && base_name[0]) ? cmd_snap_load(base_name) : false;
}

bool monitor_save_snap_psram(uint8_t slot)
{
    volatile uint8_t *dst = psram_slot_ptr(slot);
    if (!dst) {
        return false;
    }
    uint8_t hdr[256];
    size_t  hdr_len = build_snap_header(hdr);
    if (hdr_len == 0u || (sizeof(PsramSnapPrefix) + hdr_len + ORIC_RAM_SIZE) > PSRAM_SNAP_STRIDE) {
        return false;
    }

    PsramSnapPrefix pref;
    memcpy(pref.magic, "OSN1", 4);
    pref.hdr_len    = (uint16_t)hdr_len;
    pref.video_mode = ram[0x021Fu];
    uint32_t cks    = fnv1a_32(2166136261u, hdr, hdr_len);
    cks             = fnv1a_32(cks, ram, ORIC_RAM_SIZE);
    pref.checksum   = cks;

    const uint8_t *pb = (const uint8_t *)&pref;
    for (size_t i = 0; i < sizeof(pref); i++) {
        dst[i] = pb[i];
    }
    dst += sizeof(pref);
    for (size_t i = 0; i < hdr_len; i++) {
        dst[i] = hdr[i];
    }
    dst += hdr_len;
    for (size_t i = 0; i < ORIC_RAM_SIZE; i++) {
        dst[i] = ram[i];
    }
    return true;
}

bool monitor_psram_slot_valid(uint8_t slot)
{
    volatile const uint8_t *src = psram_slot_ptr(slot);
    if (!src) {
        return false;
    }
    PsramSnapPrefix pref;
    uint8_t *pb = (uint8_t *)&pref;
    for (size_t i = 0; i < sizeof(pref); i++) {
        pb[i] = src[i];
    }
    return memcmp(pref.magic, "OSN1", 4) == 0 &&
           pref.hdr_len > 12u &&
           pref.hdr_len <= 256u;
}

bool monitor_load_snap_psram(uint8_t slot)
{
    volatile const uint8_t *src = psram_slot_ptr(slot);
    if (!src) {
        return false;
    }
    PsramSnapPrefix pref;
    uint8_t *pb = (uint8_t *)&pref;
    for (size_t i = 0; i < sizeof(pref); i++) {
        pb[i] = src[i];
    }
    if (memcmp(pref.magic, "OSN1", 4) != 0 || pref.hdr_len < 12u || pref.hdr_len > 256u) {
        return false;
    }
    src += sizeof(pref);
    uint8_t hdr[256];
    for (size_t i = 0; i < pref.hdr_len; i++) {
        hdr[i] = src[i];
    }
    src += pref.hdr_len;
    uint32_t cks = fnv1a_32(2166136261u, hdr, pref.hdr_len);
    for (size_t i = 0; i < ORIC_RAM_SIZE; i++) {
        uint8_t b = src[i];
        cks ^= (uint32_t)b;
        cks *= 16777619u;
    }
    if (cks != pref.checksum) {
        return false;
    }
    for (size_t i = 0; i < ORIC_RAM_SIZE; i++) {
        ram[i] = src[i];
    }
    return apply_snap_header(hdr);
}

static bool cmd_bmp_save(const char *filename)
{
    char path[48];
    snprintf(path, sizeof(path), "%s.bmp", filename);
    HalFileHandle h = hal_file_open_write(path);
    if (!h) {
        return false;
    }

    const uint32_t width     = ORIC_SCREEN_WIDTH;
    const uint32_t height    = ORIC_SCREEN_HEIGHT;
    const uint32_t imageSize = width * height * 3u;
    const uint32_t fileSize  = 54u + imageSize;

    uint8_t header[54];
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    memcpy(&header[2],  &fileSize, 4);
    uint32_t dataOffset = 54u;
    memcpy(&header[10], &dataOffset, 4);
    uint32_t dibSize = 40u;
    memcpy(&header[14], &dibSize, 4);
    memcpy(&header[18], &width, 4);
    memcpy(&header[22], &height, 4);
    uint16_t planes = 1u;
    memcpy(&header[26], &planes, 2);
    uint16_t bpp = 24u;
    memcpy(&header[28], &bpp, 2);
    memcpy(&header[34], &imageSize, 4);

    hal_file_write(h, header, sizeof(header));

    /* Pre-calcule le pattr au debut de chaque scanline de haut en bas (224 octets)
     * afin d'ecrire le BMP directement de bas en haut en une seule passe sans fichier temp */
    uint8_t line_pattr[ORIC_SCREEN_HEIGHT];
    uint8_t fb[ORIC_FRAMEBUFFER_SIZE];
    uint8_t pattr = videoGetPattr();

    for (uint16_t y = 0; y < ORIC_SCREEN_HEIGHT; y++) {
        line_pattr[y] = pattr;
        ula_render_oric_line(&pattr, y, ram, NULL, NULL, false, fb);
    }

    static const uint8_t pal_rgb[8][3] = {
        {   0,   0,   0 }, /* noir */
        {   0,   0, 255 }, /* rouge (BGR) */
        {   0, 255,   0 }, /* vert */
        {   0, 255, 255 }, /* jaune */
        { 255,   0,   0 }, /* bleu */
        { 255,   0, 255 }, /* magenta */
        { 255, 255,   0 }, /* cyan */
        { 255, 255, 255 }  /* blanc */
    };

    uint8_t bgr_line[ORIC_SCREEN_WIDTH * 3u];
    for (int y = (int)ORIC_SCREEN_HEIGHT - 1; y >= 0; y--) {
        uint8_t p_in = line_pattr[y];
        ula_render_oric_line(&p_in, (uint16_t)y, ram, NULL, NULL, false, fb);

        uint8_t *dst = bgr_line;
        for (size_t i = 0; i < ORIC_FRAMEBUFFER_SIZE; i++) {
            uint8_t c0 = (fb[i] >> 4) & 7u;
            uint8_t c1 = fb[i] & 7u;
            *dst++ = pal_rgb[c0][0]; *dst++ = pal_rgb[c0][1]; *dst++ = pal_rgb[c0][2];
            *dst++ = pal_rgb[c1][0]; *dst++ = pal_rgb[c1][1]; *dst++ = pal_rgb[c1][2];
        }
        hal_file_write(h, bgr_line, sizeof(bgr_line));
    }

    hal_file_close(h);
    return true;
}

bool monitor_save_bmp(const char *base_name)
{
    return (base_name && base_name[0]) ? cmd_bmp_save(base_name) : false;
}

static void cmd_i2c_scan(void)
{
    mon_clear_body();
    mon_put_line(1, "    00 01 02 03 04 05 06 07");

    char line[40];
    int  found_cnt = 0;
    for (int row = 0; row < 15; row++) {
        uint8_t base_addr = (uint8_t)(row * 8);
        int pos = snprintf(line, sizeof(line), "%02X: ", base_addr);
        for (int col = 0; col < 8; col++) {
            uint8_t addr = (uint8_t)(base_addr + col);
            if (addr < 0x78u && addr >= 0x03u) {
                if (hal_i2c_probe(addr)) {
                    pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "%02X ", addr);
                    found_cnt++;
                } else {
                    pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "-- ");
                }
            } else {
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "   ");
            }
        }
        mon_put_line((uint8_t)(2 + row), line);
    }

    char status[36];
    snprintf(status, sizeof(status), "OK: I2C %d PERIPH", found_cnt);
    mon_status(true, status);
}

static void cmd_dir_sd(void)
{
    char (*list)[SD_MAX_FILENAME_LEN] = tape_shared_file_list();
    int n = hal_sd_list_files(NULL, list, SD_MAX_LIST_FILES, false);
    mon_clear_body();
    if (n < 0) {
        mon_status(false, "ERR: CARTE SD");
        return;
    }

    char line[40];
    int  shown = 0;
    for (int i = 0; i < n && shown < (int)(MON_BODY_ROWS * 2u); i += 2) {
        if (i + 1 < n) {
            snprintf(line, sizeof(line), "%-18.18s %-18.18s", list[i], list[i + 1]);
        } else {
            snprintf(line, sizeof(line), "%-18.18s", list[i]);
        }
        mon_put_line((uint8_t)(1u + (shown / 2)), line);
        shown += 2;
    }

    snprintf(line, sizeof(line), "OK: %d FICHIERS", n);
    mon_status(true, line);
}

/* --------------------------------------------------------------------------
 * Execution d'une ligne de commande
 * Retourne false si l'utilisateur demande de quitter le moniteur
 * -------------------------------------------------------------------------- */
static bool execute_monitor_line(const char *raw_cmd)
{
    const char *p = skip_ws(raw_cmd);

    /* Entree vide : repete M / D / S */
    if (*p == '\0') {
        switch (s_repeatMode) {
            case REPEAT_DUMP:
                cmd_dump_mem();
                mon_status(true, "OK: DUMP");
                break;
            case REPEAT_DISASM:
                mon_clear_body();
                cmd_disasm_page(1, MON_BODY_ROWS);
                mon_status(true, "OK: DISASM");
                break;
            case REPEAT_STEP:
                busStepInstruction();
                mon_clear_screen();
                cmd_show_regs(1);
                s_disasmAddr = vrEmu6502GetPC(g_cpu);
                cmd_disasm_page(7, MON_BODY_ROWS - 6u);
                mon_status(true, "OK: STEP");
                break;
            default:
                break;
        }
        return true;
    }

    if (strcmp(p, "Q") == 0 || strcmp(p, "X") == 0) {
        return false;
    }

    if (strcmp(p, "H") == 0 || strcmp(p, "?") == 0) {
        s_repeatMode = REPEAT_NONE;
        cmd_help();
        return true;
    }

    if (strcmp(p, "R") == 0) {
        s_repeatMode = REPEAT_NONE;
        mon_clear_body();
        cmd_show_regs(1);
        mon_status(true, "OK: REGISTRES");
        return true;
    }

    if (strcmp(p, "S") == 0) {
        s_repeatMode = REPEAT_STEP;
        busStepInstruction();
        mon_clear_screen();
        cmd_show_regs(1);
        s_disasmAddr = vrEmu6502GetPC(g_cpu);
        cmd_disasm_page(7, MON_BODY_ROWS - 6u);
        mon_status(true, "OK: STEP");
        return true;
    }

    if (p[0] == 'G' && (p[1] == '\0' || p[1] == ' ')) {
        const char *q = p + 1;
        uint16_t addr = 0;
        if (parse_hex_u16(&q, &addr)) {
            vrEmu6502SetPC(g_cpu, addr);
        }
        return false;
    }

    if (strcmp(p, "I2C") == 0) {
        s_repeatMode = REPEAT_NONE;
        cmd_i2c_scan();
        return true;
    }

    if (strcmp(p, "DIR") == 0) {
        s_repeatMode = REPEAT_NONE;
        cmd_dir_sd();
        return true;
    }

    if (strcmp(p, "TAPE") == 0) {
        s_repeatMode = REPEAT_NONE;
        tape_reset();
        mon_status(true, "OK: TAPE EJECTEE");
        return true;
    }

    if (strcmp(p, "RESET") == 0) {
        s_repeatMode = REPEAT_NONE;
        busResetMachine();
        mon_clear_screen();
        cmd_show_regs(1);
        mon_status(true, "OK: RESET ORIC");
        return true;
    }

    /* MM <addr> <val1> [val2...] */
    if (p[0] == 'M' && p[1] == 'M') {
        s_repeatMode = REPEAT_NONE;
        const char *q = p + 2;
        uint16_t addr = 0;
        uint8_t  val  = 0;
        if (!parse_hex_u16(&q, &addr) || !parse_hex_u8(&q, &val)) {
            mon_status(false, "ERR: SYNTAXE MM <ADR> <VAL>");
            return true;
        }
        int cnt = 0;
        do {
            if (addr < ORIC_RAM_SIZE) {
                ram[addr] = val;
            }
            addr++;
            cnt++;
        } while (parse_hex_u8(&q, &val));
        char msg[36];
        snprintf(msg, sizeof(msg), "OK: %d OCTET(S) ECRIT(S)", cnt);
        mon_status(true, msg);
        return true;
    }

    /* M <addr> */
    if (p[0] == 'M') {
        const char *q = p + 1;
        uint16_t addr = 0;
        if (!parse_hex_u16(&q, &addr)) {
            mon_status(false, "ERR: SYNTAXE M <ADR>");
            return true;
        }
        s_dumpAddr   = addr;
        s_repeatMode = REPEAT_DUMP;
        cmd_dump_mem();
        mon_status(true, "OK: DUMP");
        return true;
    }

    /* D <addr> */
    if (p[0] == 'D') {
        const char *q = p + 1;
        uint16_t addr = 0;
        if (!parse_hex_u16(&q, &addr)) {
            mon_status(false, "ERR: SYNTAXE D <ADR>");
            return true;
        }
        s_disasmAddr = addr;
        s_repeatMode = REPEAT_DISASM;
        mon_clear_body();
        cmd_disasm_page(1, MON_BODY_ROWS);
        mon_status(true, "OK: DISASM");
        return true;
    }

    /* FW <addr> <len> <file> */
    if (p[0] == 'F' && p[1] == 'W') {
        s_repeatMode = REPEAT_NONE;
        const char *q = p + 2;
        uint16_t addr = 0, len = 0;
        char fname[24];
        if (!parse_hex_u16(&q, &addr) || !parse_hex_u16(&q, &len) ||
            !parse_word(&q, fname, sizeof(fname))) {
            mon_status(false, "ERR: FW <ADR> <LEN> <FIC>");
            return true;
        }
        if ((uint32_t)addr + len > ORIC_RAM_SIZE) {
            mon_status(false, "ERR: HORS RAM (<C000)");
            return true;
        }
        char path[32];
        snprintf(path, sizeof(path), "%s.bin", fname);
        mon_status(hal_sd_write_file(path, &ram[addr], len) >= 0,
                   hal_sd_is_ready() ? "OK: BLOC SAUVE" : "ERR: SD WRITE");
        return true;
    }

    /* FR <addr> <file> */
    if (p[0] == 'F' && p[1] == 'R') {
        s_repeatMode = REPEAT_NONE;
        const char *q = p + 2;
        uint16_t addr = 0;
        char fname[24];
        if (!parse_hex_u16(&q, &addr) || !parse_word(&q, fname, sizeof(fname))) {
            mon_status(false, "ERR: FR <ADR> <FIC>");
            return true;
        }
        if (addr >= ORIC_RAM_SIZE) {
            mon_status(false, "ERR: ADR >= C000");
            return true;
        }
        char path[32];
        snprintf(path, sizeof(path), "%s.bin", fname);
        size_t br = 0;
        if (hal_sd_read_file(path, &ram[addr], ORIC_RAM_SIZE - addr, &br) == 0) {
            char msg[36];
            snprintf(msg, sizeof(msg), "OK: CHARGE %u OCTETS", (unsigned)br);
            mon_status(true, msg);
        } else {
            mon_status(false, "ERR: FICHIER .BIN");
        }
        return true;
    }

    /* F <addr> <len> <val> */
    if (p[0] == 'F') {
        s_repeatMode = REPEAT_NONE;
        const char *q = p + 1;
        uint16_t addr = 0, len = 0;
        uint8_t  val  = 0;
        if (!parse_hex_u16(&q, &addr) || !parse_hex_u16(&q, &len) || !parse_hex_u8(&q, &val)) {
            mon_status(false, "ERR: F <ADR> <LEN> <VAL>");
            return true;
        }
        for (uint16_t i = 0; i < len; i++) {
            uint16_t a = (uint16_t)(addr + i);
            if (a < ORIC_RAM_SIZE) {
                ram[a] = val;
            }
        }
        mon_status(true, "OK: FILL");
        return true;
    }

    /* SNS <file> / SNL <file> */
    if (strncmp(p, "SNS", 3) == 0) {
        s_repeatMode = REPEAT_NONE;
        const char *q = p + 3;
        char fname[24];
        if (!parse_word(&q, fname, sizeof(fname))) {
            mon_status(false, "ERR: SNS <FIC>");
            return true;
        }
        mon_status(cmd_snap_save(fname), "OK: SNAP SAUVE");
        return true;
    }

    if (strncmp(p, "SNL", 3) == 0) {
        s_repeatMode = REPEAT_NONE;
        const char *q = p + 3;
        char fname[24];
        if (!parse_word(&q, fname, sizeof(fname))) {
            mon_status(false, "ERR: SNL <FIC>");
            return true;
        }
        if (cmd_snap_load(fname)) {
            mon_clear_screen();
            cmd_show_regs(1);
            mon_status(true, "OK: SNAP CHARGE");
        } else {
            mon_status(false, "ERR: SNAP INTROUVABLE");
        }
        return true;
    }

    /* BMP <file> */
    if (strncmp(p, "BMP", 3) == 0) {
        s_repeatMode = REPEAT_NONE;
        const char *q = p + 3;
        char fname[24];
        if (!parse_word(&q, fname, sizeof(fname))) {
            mon_status(false, "ERR: BMP <FIC>");
            return true;
        }
        mon_status(cmd_bmp_save(fname), "OK: BMP SAUVE");
        return true;
    }

    mon_status(false, "ERR: CMD INCONNUE (H=AIDE)");
    return true;
}

/* --------------------------------------------------------------------------
 * Boucle principale du moniteur
 * -------------------------------------------------------------------------- */
void monitor_run(void)
{
    s_repeatMode = REPEAT_NONE;
    mon_clear_screen();
    cmd_show_regs(1);
    mon_status(true, "TAPEZ H POUR L'AIDE | F12:QUITTER");

    monitorEnable(true);

    CmdLine cmdline = {0};
    mon_draw_prompt(&cmdline);

    input_kbd_clear();

    bool running = true;
    while (running) {
        Ps2KeyEvent ev;
        if (!input_kbd_poll(&ev) || !ev.pressed) {
            continue;
        }

        if (ev.code == 0x07u || ev.code == 0x76u) { /* F12 ou ESC */
            break;
        }

        if (ev.code == 0x5Au) { /* ENTER */
            cmdline.buf[cmdline.len] = '\0';
            running = execute_monitor_line(cmdline.buf);
            memset(&cmdline, 0, sizeof(cmdline));
            mon_draw_prompt(&cmdline);
            continue;
        }

        if (ev.code == 0x66u) { /* BACKSPACE */
            if (cmdline.cursor > 0u) {
                memmove(&cmdline.buf[cmdline.cursor - 1u],
                        &cmdline.buf[cmdline.cursor],
                        cmdline.len - cmdline.cursor);
                cmdline.cursor--;
                cmdline.len--;
                mon_draw_prompt(&cmdline);
            }
            continue;
        }

        if (ev.code == 0x6Bu && ev.extended) { /* LEFT */
            if (cmdline.cursor > 0u) {
                cmdline.cursor--;
                mon_draw_prompt(&cmdline);
            }
            continue;
        }

        if (ev.code == 0x74u && ev.extended) { /* RIGHT */
            if (cmdline.cursor < cmdline.len) {
                cmdline.cursor++;
                mon_draw_prompt(&cmdline);
            }
            continue;
        }

        char ch = ps2_scancode_to_ascii(ev.code);
        if (ch && cmdline.len < MON_CMD_MAX - 1u) {
            memmove(&cmdline.buf[cmdline.cursor + 1u],
                    &cmdline.buf[cmdline.cursor],
                    cmdline.len - cmdline.cursor);
            cmdline.buf[cmdline.cursor] = ch;
            cmdline.cursor++;
            cmdline.len++;
            mon_draw_prompt(&cmdline);
        }
    }

    input_kbd_clear();
    oric_keyboard_clear();
    monitorEnable(false);
}
