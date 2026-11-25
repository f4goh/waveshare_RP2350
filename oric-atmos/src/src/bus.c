/*
 * Bus systeme Oric Atmos (CPU 6502, RAM/ROM, VIA 6522, ACIA 6551, boucle temps reel)
 * Version RP2350-PiZero : HDMI/DVI + Audio (HDMI/PWM/I2S) + Clavier unifie USB HID / PS/2
 */

#include "bus.h"
#include "arduino_hal.h"
#include "audio.h"
#include "config.h"
#include "config_menu.h"
#include "config_store.h"
#include "input_kbd.h"
#include "microdisc.h"
#include "monitor.h"
#include "nes_ctrl.h"
#include "oric_keyboard.h"
#include "tape.h"
#include "ula.h"
#include "video.h"

#include "pico/stdlib.h"
#include "pico/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t __aligned(4) ram[ORIC_RAM_SIZE];
uint8_t __aligned(4) overlayRam[MICRODISC_OVERLAY_SIZE];
uint8_t __aligned(4) ramMoni[ORIC_MONITOR_SIZE];

extern const uint8_t oricRom[];
extern const uint8_t microdiscRom[];

VrEmu6502  *g_cpu  = NULL;
struct via  g_via;
struct acia g_acia = {0};

static bool     s_nmiRequest  = false;
static bool     s_bannerShown = false;
static uint32_t s_bootTicks   = 0;
static uint8_t  s_latchedTape = 0;

#define MICROSECONDS_PER_BURST 50U
#define TICKS_PER_BURST        50 /* 1 MHz * 50 us */

/* --------------------------------------------------------------------------
 * Commande BASIC '!' (trap ROM $CD13)
 * -------------------------------------------------------------------------- */
static void skip_spaces_ptr(const char **p)
{
    while (**p == ' ') (*p)++;
}

static uint8_t parse_basic_num(const char **p)
{
    skip_spaces_ptr(p);
    if (**p == '#') {
        (*p)++;
        return (uint8_t)strtoul(*p, (char **)p, 16);
    }
    return (uint8_t)strtoul(*p, (char **)p, 10);
}

static void handle_bang_command(void)
{
    const char *p = (const char *)&ram[0x0036];
    skip_spaces_ptr(&p);

    if (strncmp(p, "LED", 3) == 0) {
        p += 3;
        uint8_t val = parse_basic_num(&p);
        hal_led_set(val != 0u);
    } else if (strncmp(p, "I2CW", 4) == 0) {
        p += 4;
        uint8_t addr = parse_basic_num(&p);
        if (*p == ',') p++;
        uint8_t reg = parse_basic_num(&p);
        if (*p == ',') p++;
        uint8_t val = parse_basic_num(&p);
        hal_i2c_write_reg(addr, reg, &val, 1);
    } else if (strncmp(p, "I2CR", 4) == 0) {
        p += 4;
        uint8_t addr = parse_basic_num(&p);
        if (*p == ',') p++;
        uint8_t reg = parse_basic_num(&p);
        uint8_t val = 0;
        hal_i2c_read_reg(addr, reg, &val, 1);
        ram[0x0035] = val;
    }

    uint16_t len = (uint16_t)strlen((const char *)&ram[0x0036]);
    uint16_t ptr = (uint16_t)(((uint16_t)ram[0x00EA] << 8) | ram[0x00E9]) + len;
    ram[0x00EA]  = (uint8_t)(ptr >> 8);
    ram[0x00E9]  = (uint8_t)(ptr & 0xFFu);
}

/* --------------------------------------------------------------------------
 * Bus Read / Write
 * -------------------------------------------------------------------------- */
void __not_in_flash_func(busWrite)(uint16_t addr, uint8_t val)
{
    if (addr >= ORIC_ROM_START) {
        if (microdisc_romdis() && !(microdisc_diskrom() && addr >= 0xE000u)) {
            overlayRam[addr & (ORIC_ROM_SIZE - 1u)] = val;
        }
        return; /* ROM en lecture seule */
    }

    if (addr >= ORIC_IO_START && addr < ORIC_IO_END) {
        if (microdisc_is_enabled() &&
            (addr == 0x0314u || addr == 0x0318u || (addr >= 0x0310u && addr <= 0x0313u))) {
            uint16_t op_pc = (g_cpu != NULL) ? vrEmu6502GetCurrentOpcodeAddr(g_cpu) : 0u;
            microdisc_write(addr, val, op_pc);
            return;
        }
        if (addr >= ORIC_ACIA_START && addr < ORIC_ACIA_END) {
            acia_write(&g_acia, addr & 0x03u, val);
        } else {
            /* VIA 6522 decode sur toute la page $0300-$03FF (hors Microdisc / ACIA) */
            via_write(&g_via, addr & 0x0Fu, val);
        }
        return;
    }

    ram[addr] = val;
}

uint8_t __not_in_flash_func(busRead)(uint16_t addr, bool isDbg)
{
    if (addr >= ORIC_ROM_START) {
        if (microdisc_romdis()) {
            if (microdisc_diskrom() && addr >= 0xE000u) {
                return microdiscRom[addr & (MICRODISC_ROM_SIZE - 1u)];
            }
            return overlayRam[addr & (ORIC_ROM_SIZE - 1u)];
        }
        if (!isDbg && g_cpu != NULL) {
            uint16_t op_pc = vrEmu6502GetCurrentOpcodeAddr(g_cpu);

            switch (addr) {
                case 0xE4ACu:
                    if (op_pc == 0xE4ACu) {
                        tape_on_e4ac(ram);
                    }
                    break;

                case 0xE735u:
                case 0xE736u:
                    if (op_pc == 0xE735u) {
                        if (addr == 0xE735u) {
                            tape_on_e735(ram);
                            return 0xA2u; /* LDX #$00 */
                        }
                        return 0x00u;
                    }
                    break;
                case 0xE737u:
                    if (op_pc == 0xE737u) {
                        return 0x60u; /* RTS */
                    }
                    break;

                case 0xE6C9u:
                case 0xE6CAu:
                    if (op_pc == 0xE6C9u) {
                        if (addr == 0xE6C9u) {
                            s_latchedTape = tape_read_byte();
                            return 0xA9u; /* LDA #octet */
                        }
                        return s_latchedTape;
                    }
                    break;
                case 0xE6CBu:
                    if (op_pc == 0xE6CBu) {
                        return 0x60u; /* RTS */
                    }
                    break;

                case 0xE92Cu:
                case 0xE92Du:
                case 0xE92Eu:
                    if (op_pc == 0xE92Cu) {
                        if (addr == 0xE92Cu) {
                            tape_csave(ram);
                            return 0x4Cu; /* JMP $E93B */
                        }
                        return (addr == 0xE92Du) ? 0x3Bu : 0xE9u;
                    }
                    break;

                case 0xCD13u:
                    if (op_pc == 0xCD13u) {
                        handle_bang_command();
                        return 0x60u; /* RTS */
                    }
                    break;

                case 0xF5DCu:
                case 0xF5DDu:
                    if (op_pc == addr) {
                        return 0xEAu; /* NOP */
                    }
                    break;

                default:
                    break;
            }
        }
        return oricRom[addr & (ORIC_ROM_SIZE - 1u)];
    }

    if (addr >= ORIC_IO_START && addr < ORIC_IO_END) {
        if (microdisc_is_enabled() &&
            (addr == 0x0314u || addr == 0x0318u || (addr >= 0x0310u && addr <= 0x0313u))) {
            if (isDbg) {
                return (addr == 0x0310u) ? microdisc_wd_status_reg() : microdisc_status_reg();
            }
            return microdisc_read(addr);
        }
        if (addr >= ORIC_ACIA_START && addr < ORIC_ACIA_END) {
            return isDbg ? g_acia.regs[addr & 3u] : acia_read(&g_acia, addr & 0x03u);
        }
        return isDbg ? via_mon_read(&g_via, addr & 0x0Fu) : via_read(&g_via, addr & 0x0Fu);
    }

    return ram[addr];
}

/* --------------------------------------------------------------------------
 * Initialisation, reset et pas a pas
 * -------------------------------------------------------------------------- */
static void update_cpu_irqs(void)
{
    bool irq = ((g_via.ifr & 0x80u) != 0u) ||
               acia_irq_pending(&g_acia) ||
               microdisc_irq_pending();
    *(vrEmu6502Int(g_cpu)) = irq ? IntRequested : IntCleared;
}

void busAutoTypeClear(void);

static void init_overlay_ram_pattern(void)
{
    /* Motif DRAM 4164 Oric a la mise sous tension (identique blank_ram Oricutron) :
     * indispensable pour que le test de checksum warm-boot de SEDORIC 3.0 ($C980-$FFFF)
     * ne trouve pas 0x00 sur une RAM vierge et charge bien les 60 secteurs du DOS. */
    for (size_t i = 0; i < sizeof(overlayRam); i += 256u) {
        memset(&overlayRam[i],        0x00, 128u);
        memset(&overlayRam[i + 128u], 0xFF, 128u);
    }
}

void busResetMachine(void)
{
    busAutoTypeClear();
    tape_reset();
    init_overlay_ram_pattern();
    microdisc_reset();
    via_init(&g_via);
    audioReset();
    videoSetMode(0);
    vrEmu6502Reset(g_cpu);
    oric_keyboard_clear();
    input_kbd_clear();
    s_nmiRequest  = false;
    s_bannerShown = false;
    s_bootTicks   = 0;
}

void busStepInstruction(void)
{
    uint8_t cycles = vrEmu6502InstCycle(g_cpu);
    via_clock(&g_via, cycles);
    acia_clock(&g_acia, cycles);
    microdisc_tick(cycles);
    audioStepCycles(cycles);
    update_cpu_irqs();
}

void busInit(void)
{
    hal_sd_init();
    hal_i2c_init();

    memset(ram, 0, sizeof(ram));
    init_overlay_ram_pattern();
    g_cpu = vrEmu6502New(CPU_6502U, busRead, busWrite);

    microdisc_init();
    via_init(&g_via);
    audioInit((int)ORIC_AY38910_CLOCK, (int)SAMPLE_FREQUENCY);
    acia_init(&g_acia);
    oric_keyboard_init();
    input_kbd_init();
    nes_begin();
    tape_init();
    videoInit();
    config_load();
}

/* --------------------------------------------------------------------------
 * Auto-frappe clavier (ex: CLOAD""\r depuis le Configurateur)
 * -------------------------------------------------------------------------- */
static char     s_autoBuf[32];
static uint8_t  s_autoLen   = 0;
static uint8_t  s_autoPos   = 0;
static uint32_t s_autoWait  = 0;
static uint8_t  s_autoPhase = 0; /* 0=idle/wait, 1=release */
static uint8_t  s_autoCurSc = 0;
static bool     s_autoCurSh = false;

static bool char_to_set2(char ch, uint8_t *sc_out, bool *shift_out)
{
    static const uint8_t kAlphaSc[26] = {
        0x1Cu, 0x32u, 0x21u, 0x23u, 0x24u, 0x2Bu, 0x34u, 0x33u, 0x43u, /* A..I */
        0x3Bu, 0x42u, 0x4Bu, 0x3Au, 0x31u, 0x44u, 0x4Du, 0x15u, 0x2Du, /* J..R */
        0x1Bu, 0x2Cu, 0x3Cu, 0x2Au, 0x1Du, 0x22u, 0x35u, 0x1Au  /* S..Z */
    };
    *shift_out = false;
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    if (ch >= 'A' && ch <= 'Z') {
        *sc_out = kAlphaSc[ch - 'A'];
        return true;
    }
    switch (ch) {
        case ' ':  *sc_out = 0x29u; return true;
        case '\r': *sc_out = 0x5Au; return true;
        case '"':  *sc_out = 0x52u; *shift_out = true; return true;
        case '0':  *sc_out = 0x45u; return true;
        case '1':  *sc_out = 0x16u; return true;
        default:   return false;
    }
}

void busAutoTypeClear(void)
{
    s_autoLen   = 0u;
    s_autoPos   = 0u;
    s_autoWait  = 0u;
    s_autoPhase = 0u;
}

void busAutoType(const char *str, uint16_t delay_frames)
{
    if (!str) {
        return;
    }
    size_t n = strlen(str);
    if (n >= sizeof(s_autoBuf)) {
        n = sizeof(s_autoBuf) - 1u;
    }
    memcpy(s_autoBuf, str, n);
    s_autoBuf[n] = '\0';
    s_autoLen   = (uint8_t)n;
    s_autoPos   = 0u;
    s_autoPhase = 0u;
    s_autoWait  = (uint32_t)delay_frames * 400u; /* 400 bursts * 50 us = 20 ms */
}

static void __not_in_flash_func(tick_autotype)(void)
{
    if (s_autoPos >= s_autoLen) {
        return;
    }
    if (s_autoWait > 0u) {
        s_autoWait--;
        return;
    }

    Ps2KeyEvent ev;
    ev.extended = false;
    ev.source   = 1u;

    if (s_autoPhase == 0u) {
        uint8_t sc = 0u;
        bool    sh = false;
        while (s_autoPos < s_autoLen && !char_to_set2(s_autoBuf[s_autoPos], &sc, &sh)) {
            s_autoPos++;
        }
        if (s_autoPos >= s_autoLen) {
            return;
        }
        s_autoCurSc = sc;
        s_autoCurSh = sh;
        if (sh) {
            ev.code = 0x12u; /* LShift */
            ev.pressed = true;
            oric_keyboard_apply_event(&ev);
        }
        ev.code = sc;
        ev.pressed = true;
        oric_keyboard_apply_event(&ev);
        s_autoPhase = 1u;
        s_autoWait  = 600u; /* ~30 ms appui */
    } else {
        ev.code = s_autoCurSc;
        ev.pressed = false;
        oric_keyboard_apply_event(&ev);
        if (s_autoCurSh) {
            ev.code = 0x12u;
            ev.pressed = false;
            oric_keyboard_apply_event(&ev);
        }
        s_autoPos++;
        s_autoPhase = 0u;
        s_autoWait  = 600u; /* ~30 ms pause */
    }
}

/* --------------------------------------------------------------------------
 * Entrees clavier USB HID + PS/2 + Manette NES
 * -------------------------------------------------------------------------- */
static void __not_in_flash_func(poll_keyboard_input)(void)
{
    Ps2KeyEvent ev;
    while (input_kbd_poll(&ev)) {
        if (ev.pressed && !ev.extended) {
            if (ev.code == 0x05u) { /* F1 : NMI */
                s_nmiRequest = true;
                continue;
            }
            if (ev.code == 0x06u) { /* F2 : Reset */
                busResetMachine();
                return;
            }
            if (ev.code == 0x04u) { /* F3 : Moniteur */
                monitor_run();
                return;
            }
            if (ev.code == 0x0Cu || ev.code == 0x78u || ev.code == 0x07u) { /* F4, F11 ou F12 : Configurateur */
                config_menu_run();
                return;
            }
        }

        oric_keyboard_apply_event(&ev);
    }
}

static void __not_in_flash_func(poll_nes_input)(void)
{
    static uint8_t s_prevNesBits = 0xFFu;
    uint8_t nes_bits = 0xFFu;
    if (nes_poll_nonblocking(&nes_bits)) {
        if ((nes_bits & 0x30u) == 0u && (s_prevNesBits & 0x30u) != 0u) {
            s_prevNesBits = nes_bits;
            config_menu_run();
            return;
        }
        s_prevNesBits = nes_bits;
        oric_keyboard_nes(nes_bits);
    }
}

/* --------------------------------------------------------------------------
 * Boucle principale temps reel (1 MHz Oric)
 * -------------------------------------------------------------------------- */
void __not_in_flash_func(busMainLoop)(void)
{
    vrEmu6502Reset(g_cpu);

    absolute_time_t currentTime = get_absolute_time();
    int tickDebt = 0;

    while (1) {
        poll_keyboard_input();
        poll_nes_input();
        tick_autotype();

        if (!s_bannerShown) {
            if (s_bootTicks == 50000u) {
                if (ram[ORIC_SCREEN_ADDR + 40u * 7u + 2u] == 'R') {
                    memcpy(ram + ORIC_SCREEN_ADDR + 40u * 3u + 2u,
                           "\x03\x14ORIC RP2350 | F4:CFG F3:MON\x17\x00",
                           31);
                }
                s_bannerShown = true;
            }
            s_bootTicks++;
        }

        while (tickDebt < TICKS_PER_BURST) {
            uint8_t cycles = vrEmu6502InstCycle(g_cpu);

            via_clock(&g_via, cycles);
            acia_clock(&g_acia, cycles);
            microdisc_tick(cycles);
            audioStepCycles(cycles);

            update_cpu_irqs();
            *(vrEmu6502Nmi(g_cpu)) = s_nmiRequest ? IntRequested : IntCleared;

            tickDebt += cycles;
        }

        if (microdisc_should_autoflush()) {
            microdisc_autoflush();
        }

        tickDebt -= TICKS_PER_BURST;
        s_nmiRequest = false;

        currentTime = delayed_by_us(currentTime, MICROSECONDS_PER_BURST);
        uint64_t now_us = to_us_since_boot(get_absolute_time());
        uint64_t target_us = to_us_since_boot(currentTime);
        if (now_us > target_us) {
            if ((now_us - target_us) > 10000ULL) {
                currentTime = get_absolute_time();
            }
        } else {
            busy_wait_until(currentTime);
        }
    }
}
