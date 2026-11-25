/*
 *  1541fs.cpp - 1541 emulation in host FatFs directory (/c64 on MicroSD)
 *
 *  Allows SAVE"NAME",8, LOAD"NAME",8, and LOAD"$",8 directly on /c64/*.PRG
 *  when no .D64 disk image is mounted.
 */

#include "sysdeps.h"
#include "1541fs.h"
#include "IEC.h"

#include <cstring>
#include <cctype>

extern "C" {
void disk_loader_scan(void);
}

static bool fs_match(const char *p, const char *n)
{
    if (!*n) return false;
    while (*p) {
        if (*p == '*') {
            return true;
        } else if (*p == '?') {
            if (!*n) return false;
            p++;
            n++;
        } else {
            if (toupper((unsigned char)*p) != toupper((unsigned char)*n)) {
                return false;
            }
            p++;
            n++;
        }
    }
    return (*n == '\0');
}

static void strip_drive_prefix(char *name)
{
    // Strip optional "@0:", "0:", "@" prefix from C64 SAVE/OPEN filenames
    char *p = name;
    if (p[0] == '@') {
        p++;
    }
    if (p[0] >= '0' && p[0] <= '9' && p[1] == ':') {
        p += 2;
    } else if (p[0] == ':') {
        p++;
    }
    if (p != name) {
        memmove(name, p, strlen(p) + 1);
    }
}

static bool has_prg_ext(const char *name)
{
    size_t len = strlen(name);
    if (len < 4) return false;
    return strcasecmp(name + len - 4, ".prg") == 0;
}

static void sanitize_filename(char *name)
{
    for (char *p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c < 32) {
            *p = '_';
        } else {
            *p = (char)toupper(c);
        }
    }
}

FSDrive::FSDrive(IEC *iec, const std::string & path) : Drive(iec)
{
    for (unsigned i = 0; i < 16; ++i) {
        fil_open[i] = false;
        fil_writing[i] = false;
        has_read_char[i] = false;
        read_char[i] = 0;
        dir_pos[i] = 0;
    }
    change_dir(path);
    Reset();
    Ready = true;
}

FSDrive::~FSDrive()
{
    close_all_channels();
    Ready = false;
}

bool FSDrive::change_dir(const std::string & path)
{
    dir_path = path.empty() ? "/c64" : path;
    memset(dir_title, ' ', sizeof(dir_title));
    const char *title = "SD /C64";
    for (size_t i = 0; title[i] && i < sizeof(dir_title); ++i) {
        dir_title[i] = ascii2petscii(title[i]);
    }
    return true;
}

void FSDrive::close_all_channels()
{
    for (int i = 0; i < 16; ++i) {
        Close(i);
    }
}

void FSDrive::Reset()
{
    close_all_channels();
    set_error(ERR_OK);
}

uint8_t FSDrive::Open(int channel, const uint8_t *name, int name_len)
{
    set_error(ERR_OK);

    if (channel == 15) {
        execute_cmd(name, name_len);
        return ST_OK;
    }

    Close(channel);

    if (name_len <= 0 || name[0] == '#') {
        set_error(ERR_NOCHANNEL);
        return ST_OK;
    }

    // Ensure /c64 directory exists
    f_mkdir(dir_path.c_str());

    if (name[0] == '$') {
        return open_directory(channel, name + 1, name_len - 1);
    }

    return open_file(channel, name, name_len);
}

uint8_t FSDrive::open_file(int channel, const uint8_t *name, int name_len)
{
    char plain_name[NAMEBUF_LENGTH];
    int plain_name_len = 0;
    int mode = FMODE_READ;
    int type = FTYPE_PRG;
    int rec_len = 0;

    parse_file_name(name, name_len, (uint8_t *)plain_name, plain_name_len, mode, type, rec_len, true);
    plain_name[plain_name_len] = '\0';
    strip_drive_prefix(plain_name);

    // Channel 0 is READ (LOAD), channel 1 is WRITE (SAVE)
    if (channel == 0 || channel == 1) {
        mode = channel ? FMODE_WRITE : FMODE_READ;
        if (type == FTYPE_DEL) {
            type = FTYPE_PRG;
        }
    }

    if (plain_name[0] == '\0') {
        set_error(ERR_SYNTAX34);
        return ST_OK;
    }

    if (mode == FMODE_WRITE || mode == FMODE_APPEND) {
        if (strchr(plain_name, '*') || strchr(plain_name, '?')) {
            set_error(ERR_SYNTAX33);
            return ST_OK;
        }
        sanitize_filename(plain_name);

        std::string full_path = dir_path + "/" + plain_name;
        if (!has_prg_ext(plain_name)) {
            full_path += ".PRG";
        }

        BYTE fmode = FA_WRITE | ((mode == FMODE_APPEND) ? FA_OPEN_APPEND : FA_CREATE_ALWAYS);
        if (f_open(&fil[channel], full_path.c_str(), fmode) != FR_OK) {
            set_error(ERR_WRITE25);
            return ST_OK;
        }
        fil_open[channel] = true;
        fil_writing[channel] = true;
        LED = DRVLED_ON;
        return ST_OK;
    }

    // FMODE_READ: search matching file in /c64
    std::string matched_path;
    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, dir_path.c_str()) == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
            if (fno.fattrib & AM_DIR) continue;

            // Match exact name or name without .PRG extension
            char base[64];
            strncpy(base, fno.fname, sizeof(base) - 1);
            base[sizeof(base) - 1] = '\0';

            if (fs_match(plain_name, base)) {
                matched_path = dir_path + "/" + fno.fname;
                break;
            }
            if (has_prg_ext(base)) {
                base[strlen(base) - 4] = '\0';
                if (fs_match(plain_name, base)) {
                    matched_path = dir_path + "/" + fno.fname;
                    break;
                }
            }
        }
        f_closedir(&dir);
    }

    if (matched_path.empty()) {
        set_error(ERR_FILENOTFOUND);
        return ST_OK;
    }

    if (f_open(&fil[channel], matched_path.c_str(), FA_READ) != FR_OK) {
        set_error(ERR_FILENOTFOUND);
        return ST_OK;
    }

    fil_open[channel] = true;
    fil_writing[channel] = false;
    has_read_char[channel] = false;

    UINT br = 0;
    if (f_read(&fil[channel], &read_char[channel], 1, &br) == FR_OK && br == 1) {
        has_read_char[channel] = true;
    }
    LED = DRVLED_ON;
    return ST_OK;
}

uint8_t FSDrive::open_directory(int channel, const uint8_t *pattern, int pattern_len)
{
    char pat[NAMEBUF_LENGTH];
    if (pattern_len > 0) {
        int plen = 0;
        int mode = FMODE_READ, type = FTYPE_PRG, rec_len = 0;
        parse_file_name(pattern, pattern_len, (uint8_t *)pat, plen, mode, type, rec_len, true);
        pat[plen] = '\0';
        strip_drive_prefix(pat);
    } else {
        strcpy(pat, "*");
    }

    auto &buf = dir_buf[channel];
    buf.clear();
    dir_pos[channel] = 0;

    // BASIC load address $0401
    buf.push_back(0x01);
    buf.push_back(0x04);

    // Header line: 0 "SD /C64         " SD 2A
    buf.push_back(0x01);
    buf.push_back(0x01);
    buf.push_back(0x00);
    buf.push_back(0x00);
    buf.push_back(0x12); // RVS ON
    buf.push_back('"');
    for (int i = 0; i < 16; ++i) buf.push_back(dir_title[i]);
    buf.push_back('"');
    buf.push_back(' ');
    buf.push_back('S');
    buf.push_back('D');
    buf.push_back(' ');
    buf.push_back('2');
    buf.push_back('A');
    buf.push_back(0x00);

    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, dir_path.c_str()) == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
            if (fno.fattrib & AM_DIR) continue;

            // Show .PRG files cleanly
            if (!has_prg_ext(fno.fname)) continue;

            char disp[20];
            strncpy(disp, fno.fname, sizeof(disp) - 1);
            disp[sizeof(disp) - 1] = '\0';
            if (has_prg_ext(disp)) {
                disp[strlen(disp) - 4] = '\0';
            }

            if (pat[0] && !fs_match(pat, disp) && !fs_match(pat, fno.fname)) {
                continue;
            }

            uint16_t blocks = (uint16_t)((fno.fsize + 253) / 254);
            if (blocks == 0 && fno.fsize > 0) blocks = 1;

            buf.push_back(0x01);
            buf.push_back(0x01);
            buf.push_back((uint8_t)(blocks & 0xFF));
            buf.push_back((uint8_t)(blocks >> 8));

            if (blocks < 10) buf.push_back(' ');
            if (blocks < 100) buf.push_back(' ');
            if (blocks < 1000) buf.push_back(' ');

            buf.push_back('"');
            size_t nlen = strlen(disp);
            if (nlen > 16) nlen = 16;
            for (size_t i = 0; i < nlen; ++i) {
                buf.push_back(ascii2petscii(disp[i]));
            }
            buf.push_back('"');
            for (size_t i = nlen; i < 16; ++i) {
                buf.push_back(' ');
            }
            buf.push_back(' ');
            buf.push_back('P');
            buf.push_back('R');
            buf.push_back('G');
            buf.push_back(' ');
            buf.push_back(0x00);
        }
        f_closedir(&dir);
    }

    // Footer: 664 BLOCKS FREE.
    buf.push_back(0x01);
    buf.push_back(0x01);
    buf.push_back(0x98);
    buf.push_back(0x02);
    const char *free_str = "BLOCKS FREE.             ";
    for (int i = 0; free_str[i]; ++i) {
        buf.push_back((uint8_t)free_str[i]);
    }
    buf.push_back(0x00);
    buf.push_back(0x00);
    buf.push_back(0x00);

    LED = DRVLED_ON;
    return ST_OK;
}

uint8_t FSDrive::Close(int channel)
{
    if (!dir_buf[channel].empty()) {
        dir_buf[channel].clear();
        dir_pos[channel] = 0;
    }

    if (fil_open[channel]) {
        bool was_writing = fil_writing[channel];
        f_close(&fil[channel]);
        fil_open[channel] = false;
        fil_writing[channel] = false;
        has_read_char[channel] = false;
        if (was_writing) {
            disk_loader_scan();
        }
    }

    LED = DRVLED_OFF;
    return ST_OK;
}

uint8_t FSDrive::Read(int channel, uint8_t &byte)
{
    if (channel == 15) {
        byte = (uint8_t)*error_ptr++;
        if (byte == '\r') {
            set_error(ERR_OK);
            return ST_EOF;
        }
        return ST_OK;
    }

    if (!dir_buf[channel].empty()) {
        if (dir_pos[channel] >= dir_buf[channel].size()) {
            return ST_EOF;
        }
        byte = dir_buf[channel][dir_pos[channel]++];
        if (dir_pos[channel] >= dir_buf[channel].size()) {
            return ST_EOF;
        }
        return ST_OK;
    }

    if (!fil_open[channel] || fil_writing[channel] || !has_read_char[channel]) {
        return ST_READ_TIMEOUT;
    }

    byte = read_char[channel];

    UINT br = 0;
    if (f_read(&fil[channel], &read_char[channel], 1, &br) == FR_OK && br == 1) {
        return ST_OK;
    }

    has_read_char[channel] = false;
    return ST_EOF;
}

uint8_t FSDrive::Write(int channel, uint8_t byte, bool eoi)
{
    (void)eoi;
    if (channel == 15) {
        return ST_OK;
    }

    if (!fil_open[channel] || !fil_writing[channel]) {
        set_error(ERR_WRITE25);
        return ST_TIMEOUT;
    }

    UINT bw = 0;
    if (f_write(&fil[channel], &byte, 1, &bw) != FR_OK || bw != 1) {
        set_error(ERR_WRITE25);
        return ST_TIMEOUT;
    }

    return ST_OK;
}

void FSDrive::initialize_cmd()
{
    close_all_channels();
}

void FSDrive::validate_cmd()
{
}
