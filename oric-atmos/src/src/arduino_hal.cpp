/*
 * Pont minimal C <-> Arduino (Earle Philhower) pour SD et Wire (I2C)
 * Seul fichier C++ du projet : isole completement les classes C++ Arduino (SD, SPI, Wire).
 */

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>
#include <strings.h>

#include "arduino_hal.h"

#ifdef NO_USB
/* Stub SerialUSB inactif requis par SdFat/PioSdioCard lorsque TinyUSB Host possede rhport 0 */
SerialUSB::SerialUSB() {}
void SerialUSB::begin(unsigned long baud) { (void)baud; }
void SerialUSB::end() {}
int SerialUSB::peek() { return -1; }
int SerialUSB::read() { return -1; }
int SerialUSB::available() { return 0; }
int SerialUSB::availableForWrite() { return 0; }
void SerialUSB::flush() {}
size_t SerialUSB::write(uint8_t c) { (void)c; return 1u; }
size_t SerialUSB::write(const uint8_t *p, size_t len) { (void)p; return len; }
SerialUSB::operator bool() { return false; }
bool SerialUSB::dtr() { return false; }
bool SerialUSB::rts() { return false; }
void SerialUSB::ignoreFlowControl(bool ignore) { (void)ignore; }
void SerialUSB::interfaceCB(int itf, uint8_t *dst, int len) { (void)itf; (void)dst; (void)len; }
void SerialUSB::checkSerialReset() {}
void SerialUSB::tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) { (void)itf; (void)dtr; (void)rts; }
void SerialUSB::tud_cdc_line_coding_cb(uint8_t itf, void const *p) { (void)itf; (void)p; }
SerialUSB Serial;
namespace arduino {
void serialEventRun(void) {}
}
#endif

/* --------------------------------------------------------------------------
 * Etat SD et cache lecture sequentielle TAP (512 octets au lieu de 64 Ko)
 * -------------------------------------------------------------------------- */
#define TAPE_CACHE_SIZE 512U

static bool     s_sdReady = false;
static File     s_tapeFile;
static bool     s_tapeOpen = false;
static File     s_writeFile;
static bool     s_writeOpen = false;
static File     s_readFile;
static bool     s_readOpen = false;
static uint32_t s_tapeSize = 0;
static uint32_t s_tapePos = 0;
static uint8_t  s_tapeCache[TAPE_CACHE_SIZE];
static uint32_t s_cacheBase = 0;
static size_t   s_cacheLen = 0;

void hal_led_set(bool on)
{
    digitalWrite(ORIC_PIN_LED, on ? HIGH : LOW);
}

bool hal_sd_init(void)
{
    pinMode(ORIC_PIN_LED, OUTPUT);
    hal_led_set(false);

    /* MicroSD integre RP2350-PiZero sur SPI1 (SCK=30, MOSI=31, MISO=40, CS logiciel=43) */
    SPI1.setRX(PIN_SD_MISO);
    SPI1.setTX(PIN_SD_MOSI);
    SPI1.setSCK(PIN_SD_SCK);

    s_sdReady = SD.begin(PIN_SD_CS, SD_SCK_MHZ(20), SPI1);
    return s_sdReady;
}

bool hal_sd_is_ready(void)
{
    if (!s_sdReady) {
        s_sdReady = SD.begin(PIN_SD_CS, SD_SCK_MHZ(20), SPI1);
    }
    return s_sdReady;
}

int hal_sd_list_files(const char *ext,
                      char names[][SD_MAX_FILENAME_LEN],
                      size_t max_files,
                      bool strip_ext)
{
    if (!hal_sd_is_ready()) {
        return -1;
    }

    File root = SD.open("/");
    if (!root || !root.isDirectory()) {
        return -1;
    }

    const size_t ext_len = ext ? strlen(ext) : 0;
    size_t count = 0;

    while (count < max_files) {
        File entry = root.openNextFile();
        if (!entry) {
            break;
        }
        if (entry.isDirectory()) {
            entry.close();
            continue;
        }

        const char *name = entry.name();
        size_t len = strlen(name);

        /* Ignore les fichiers caches macOS / systeme */
        if (name[0] == '.') {
            entry.close();
            continue;
        }

        bool match = true;
        if (ext_len > 0) {
            match = (len > ext_len) && (strcasecmp(&name[len - ext_len], ext) == 0);
        }

        if (match) {
            size_t copy_len = (strip_ext && ext_len > 0) ? (len - ext_len) : len;
            if (copy_len >= SD_MAX_FILENAME_LEN) {
                copy_len = SD_MAX_FILENAME_LEN - 1U;
            }
            memcpy(names[count], name, copy_len);
            names[count][copy_len] = '\0';
            count++;
        }

        entry.close();
    }

    root.close();
    return (int)count;
}

int hal_sd_read_file(const char *filename, uint8_t *buf, size_t max_size, size_t *bytes_read)
{
    if (bytes_read) {
        *bytes_read = 0;
    }
    if (!hal_sd_is_ready() || !filename || !buf) {
        return -1;
    }

    File f = SD.open(filename, FILE_READ);
    if (!f) {
        return -1;
    }

    size_t fsize = (size_t)f.size();
    if (fsize > max_size) {
        f.close();
        return -2;
    }

    size_t br = f.read(buf, fsize);
    f.close();

    if (bytes_read) {
        *bytes_read = br;
    }
    return 0;
}

int hal_sd_write_file(const char *filename, const uint8_t *buf, size_t size)
{
    if (!hal_sd_is_ready() || !filename || !buf) {
        return -1;
    }

    File f = SD.open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) {
        return -1;
    }

    size_t written = f.write(buf, size);
    f.flush();
    f.close();
    return (written == size) ? (int)written : -2;
}

int hal_sd_save_snap(const char *filename,
                     const uint8_t *hdr, size_t hdr_size,
                     const uint8_t *ram, size_t ram_size)
{
    if (!hal_sd_is_ready() || !filename) {
        return -1;
    }

    File f = SD.open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) {
        return -1;
    }

    size_t written = 0;
    written += f.write(hdr, hdr_size);
    written += f.write(ram, ram_size);
    f.flush();
    f.close();

    return (written == hdr_size + ram_size) ? (int)written : -2;
}

int hal_sd_load_snap(const char *filename,
                     uint8_t *hdr, size_t hdr_size,
                     uint8_t *ram, size_t ram_size)
{
    if (!hal_sd_is_ready() || !filename) {
        return -1;
    }

    File f = SD.open(filename, O_RDONLY);
    if (!f) {
        return -1;
    }

    size_t total = 0;
    total += f.read(hdr, hdr_size);
    total += f.read(ram, ram_size);
    f.close();

    return (total == hdr_size + ram_size) ? (int)total : -2;
}

HalFileHandle hal_file_open_write(const char *filename)
{
    if (!hal_sd_is_ready() || !filename) {
        return nullptr;
    }
    if (s_writeOpen) {
        s_writeFile.close();
        s_writeOpen = false;
    }

    s_writeFile = SD.open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (!s_writeFile) {
        return nullptr;
    }
    s_writeOpen = true;
    return (HalFileHandle)&s_writeFile;
}

bool hal_file_write(HalFileHandle h, const uint8_t *data, size_t len)
{
    if (!h || !s_writeOpen || !data) {
        return false;
    }
    return s_writeFile.write(data, len) == len;
}

void hal_file_close(HalFileHandle h)
{
    if (!h || !s_writeOpen) {
        return;
    }
    s_writeFile.flush();
    s_writeFile.close();
    s_writeOpen = false;
}

HalFileHandle hal_file_open_read(const char *filename, uint32_t *size_out)
{
    if (size_out) {
        *size_out = 0;
    }
    if (!hal_sd_is_ready() || !filename) {
        return nullptr;
    }
    if (s_readOpen) {
        s_readFile.close();
        s_readOpen = false;
    }

    s_readFile = SD.open(filename, FILE_READ);
    if (!s_readFile) {
        return nullptr;
    }
    s_readOpen = true;
    if (size_out) {
        *size_out = (uint32_t)s_readFile.size();
    }
    return (HalFileHandle)&s_readFile;
}

size_t hal_file_read(HalFileHandle h, uint8_t *data, size_t len)
{
    if (!h || !s_readOpen || !data || len == 0) {
        return 0;
    }
    int r = s_readFile.read(data, len);
    return (r > 0) ? (size_t)r : 0;
}

void hal_file_close_read(HalFileHandle h)
{
    if (!h || !s_readOpen) {
        return;
    }
    s_readFile.close();
    s_readOpen = false;
}

/* --------------------------------------------------------------------------
 * Lecteur TAP en streaming sur SD
 * -------------------------------------------------------------------------- */
bool hal_tape_open(const char *filename)
{
    hal_tape_close();

    if (!hal_sd_is_ready() || !filename) {
        return false;
    }

    s_tapeFile = SD.open(filename, FILE_READ);
    if (!s_tapeFile) {
        return false;
    }

    s_tapeSize = (uint32_t)s_tapeFile.size();
    s_tapePos = 0;
    s_cacheBase = 0;
    s_cacheLen = 0;
    s_tapeOpen = true;
    return true;
}

void hal_tape_close(void)
{
    if (s_tapeOpen) {
        s_tapeFile.close();
        s_tapeOpen = false;
    }
    s_tapeSize = 0;
    s_tapePos = 0;
    s_cacheBase = 0;
    s_cacheLen = 0;
}

bool hal_tape_is_open(void)
{
    return s_tapeOpen;
}

uint32_t hal_tape_size(void)
{
    return s_tapeSize;
}

uint32_t hal_tape_tell(void)
{
    return s_tapePos;
}

bool hal_tape_seek(uint32_t pos)
{
    if (!s_tapeOpen || pos > s_tapeSize) {
        return false;
    }
    s_tapePos = pos;
    return true;
}

bool hal_tape_read_byte(uint8_t *out)
{
    if (!s_tapeOpen || s_tapePos >= s_tapeSize) {
        return false;
    }

    if (s_tapePos < s_cacheBase || s_tapePos >= (s_cacheBase + s_cacheLen)) {
        if (!s_tapeFile.seek(s_tapePos)) {
            return false;
        }
        int n = s_tapeFile.read(s_tapeCache, TAPE_CACHE_SIZE);
        if (n <= 0) {
            s_cacheLen = 0;
            return false;
        }
        s_cacheBase = s_tapePos;
        s_cacheLen = (size_t)n;
    }

    *out = s_tapeCache[s_tapePos - s_cacheBase];
    s_tapePos++;
    return true;
}

/* --------------------------------------------------------------------------
 * I2C (Wire1 : broches header 3=GPIO2 SDA, 5=GPIO3 SCL)
 * -------------------------------------------------------------------------- */
void hal_i2c_init(void)
{
    Wire1.setSDA(PIN_I2C_SDA);
    Wire1.setSCL(PIN_I2C_SCL);
    Wire1.setClock(400000);
    Wire1.begin();
}

bool hal_i2c_probe(uint8_t addr)
{
    Wire1.beginTransmission(addr);
    return Wire1.endTransmission() == 0;
}

bool hal_i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *buf, uint8_t len)
{
    Wire1.beginTransmission(addr);
    Wire1.write(reg);
    if (buf && len > 0) {
        Wire1.write(buf, len);
    }
    return Wire1.endTransmission() == 0;
}

int hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
    if (!buf || len == 0) {
        return 0;
    }
    Wire1.beginTransmission(addr);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) {
        return -1;
    }
    uint8_t got = Wire1.requestFrom(addr, len);
    for (uint8_t i = 0; i < got && Wire1.available(); i++) {
        buf[i] = (uint8_t)Wire1.read();
    }
    return (int)got;
}
