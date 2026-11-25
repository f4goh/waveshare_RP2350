/*
 * Pilote clavier PS/2 via PIO2 SM0 + decodeur Set-2 (Waveshare RP2350-PiZero)
 */

#include "ps2_kbd.h"
#include "config.h"
#include "ps2-kbd.pio.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/time.h"

#define PS2_QUEUE_SIZE 32U
#define PS2_QUEUE_MASK (PS2_QUEUE_SIZE - 1U)

/* PIO0 = HDMI/DVI, PIO1 = PIO-USB Host, PIO2 = PS/2 (SM0) + I2S (SM1) */
static PIO     s_ps2Pio = pio2;
static int     s_ps2Sm  = -1;

static volatile uint8_t s_queue[PS2_QUEUE_SIZE];
static volatile uint8_t s_qHead = 0;
static volatile uint8_t s_qTail = 0;

bool ps2kbd_begin(void)
{
    if (!pio_can_add_program(s_ps2Pio, &ps2kbd_program)) {
        return false;
    }
    int sm = pio_claim_unused_sm(s_ps2Pio, true);
    if (sm < 0) {
        return false;
    }
    s_ps2Sm = sm;

    uint offset = pio_add_program(s_ps2Pio, &ps2kbd_program);
    pio_sm_config c = ps2kbd_program_get_default_config(offset);

    pio_gpio_init(s_ps2Pio, PIN_PS2_CLOCK);
    pio_gpio_init(s_ps2Pio, PIN_PS2_DATA);
    /* Pull-ups obligatoires : evite les parasites si aucun clavier PS/2 n'est branche */
    gpio_pull_up(PIN_PS2_CLOCK);
    gpio_pull_up(PIN_PS2_DATA);

    sm_config_set_in_pins(&c, PIN_PS2_DATA);
    sm_config_set_set_pins(&c, PIN_PS2_DATA, 2);
    sm_config_set_out_pins(&c, PIN_PS2_DATA, 1);
    sm_config_set_jmp_pin(&c, PIN_PS2_CLOCK);

    pio_sm_set_pindirs_with_mask(s_ps2Pio, (uint)s_ps2Sm, 0u,
                                 (1u << PIN_PS2_CLOCK) | (1u << PIN_PS2_DATA));

    sm_config_set_in_shift(&c, true, true, 11);
    sm_config_set_out_shift(&c, true, true, 32);
    /* ~125 kHz PIO quelle que soit la frequence CPU (ex: 252 MHz -> 2016) */
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 125000.0f);

    pio_sm_clear_fifos(s_ps2Pio, (uint)s_ps2Sm);
    pio_sm_init(s_ps2Pio, (uint)s_ps2Sm, offset, &c);
    pio_sm_set_enabled(s_ps2Pio, (uint)s_ps2Sm, true);

    return true;
}

uint8_t __not_in_flash_func(ps2kbd_read)(void)
{
    if (s_ps2Sm < 0 || pio_sm_is_rx_fifo_empty(s_ps2Pio, (uint)s_ps2Sm)) {
        return 0;
    }

    uint32_t raw = pio_sm_get_blocking(s_ps2Pio, (uint)s_ps2Sm);
    raw >>= 21;
    uint8_t val = (uint8_t)((raw >> 1) & 0xFFu);

    if (val == 0xAAu) {
        /* Clavier PS/2 detecte : allume la LED NumLock uniquement s'il est present */
        sleep_us(350);
        ps2kbd_write(0xEDu);
        ps2kbd_write(0x02u);
        return 0;
    }
    if (val == 0xFEu || val == 0xFAu) {
        return 0;
    }
    return val;
}

void ps2kbd_write(uint8_t value)
{
    if (s_ps2Sm < 0) {
        return;
    }

    uint32_t frame = value;
    uint8_t ones = 0;
    for (int i = 0; i < 8; i++) {
        ones += (uint8_t)((value >> i) & 1u);
    }

    frame |= 0x600u;
    if ((ones & 1u) == 0u) {
        frame |= 0x100u; /* Parite impaire */
    }

    pio_sm_put_blocking(s_ps2Pio, (uint)s_ps2Sm, ~frame);
}

void __not_in_flash_func(ps2_queue_push)(uint8_t raw_byte)
{
    if (raw_byte == 0u) {
        return;
    }
    uint8_t next = (uint8_t)((s_qHead + 1u) & PS2_QUEUE_MASK);
    if (next != s_qTail) {
        s_queue[s_qHead] = raw_byte;
        __dmb();
        s_qHead = next;
    }
}

bool __not_in_flash_func(ps2_queue_pop)(uint8_t *raw_byte)
{
    if (s_qTail == s_qHead) {
        return false;
    }
    *raw_byte = s_queue[s_qTail];
    __dmb();
    s_qTail = (uint8_t)((s_qTail + 1u) & PS2_QUEUE_MASK);
    return true;
}

void ps2_queue_clear(void)
{
    s_qTail = s_qHead;
}

void ps2_decoder_reset(Ps2Decoder *dec)
{
    dec->extended = false;
    dec->release  = false;
}

static bool s_ps2Azerty = false;

void ps2_set_azerty(bool azerty_fr)
{
    s_ps2Azerty = azerty_fr;
}

bool ps2_is_azerty(void)
{
    return s_ps2Azerty;
}

static uint8_t normalize_ps2_set2(uint8_t sc, bool extended)
{
    if (!s_ps2Azerty || extended) {
        return sc;
    }
    switch (sc) {
        case 0x15u: return 0x1Cu; /* A (AZERTY) -> A (Oric) */
        case 0x1Cu: return 0x15u; /* Q (AZERTY) -> Q (Oric) */
        case 0x1Au: return 0x1Du; /* W (AZERTY) -> W (Oric) */
        case 0x1Du: return 0x1Au; /* Z (AZERTY) -> Z (Oric) */
        case 0x4Cu: return 0x3Au; /* M (AZERTY) -> M (Oric) */
        case 0x3Au: return 0x41u; /* , (AZERTY) -> , (Oric) */
        case 0x41u: return 0x4Cu; /* ; (AZERTY) -> ; (Oric) */
        default:    return sc;
    }
}

bool ps2_decoder_feed(Ps2Decoder *dec, uint8_t raw_byte, Ps2KeyEvent *ev)
{
    if (raw_byte == 0u) {
        return false;
    }
    if (raw_byte == 0xE0u) {
        dec->extended = true;
        return false;
    }
    if (raw_byte == 0xF0u) {
        dec->release = true;
        return false;
    }

    ev->code     = normalize_ps2_set2(raw_byte, dec->extended);
    ev->extended = dec->extended;
    ev->pressed  = !dec->release;
    ev->source   = 0u;

    dec->extended = false;
    dec->release  = false;
    return true;
}

char ps2_scancode_to_ascii(uint8_t code)
{
    static const char map[128] = {
        [0x16] = '1', [0x1E] = '2', [0x26] = '3', [0x25] = '4', [0x2E] = '5',
        [0x36] = '6', [0x3D] = '7', [0x3E] = '8', [0x46] = '9', [0x45] = '0',

        [0x1C] = 'A', [0x32] = 'B', [0x21] = 'C', [0x23] = 'D', [0x24] = 'E',
        [0x2B] = 'F', [0x34] = 'G', [0x33] = 'H', [0x43] = 'I', [0x3B] = 'J',
        [0x42] = 'K', [0x4B] = 'L', [0x3A] = 'M', [0x31] = 'N', [0x44] = 'O',
        [0x4D] = 'P', [0x15] = 'Q', [0x2D] = 'R', [0x1B] = 'S', [0x2C] = 'T',
        [0x3C] = 'U', [0x2A] = 'V', [0x1D] = 'W', [0x22] = 'X', [0x35] = 'Y',
        [0x1A] = 'Z',

        [0x29] = ' ', [0x4A] = '?', [0x49] = '.', [0x4E] = '-', [0x41] = ',',
        [0x70] = '0', [0x69] = '1', [0x72] = '2', [0x7A] = '3', [0x6B] = '4',
        [0x73] = '5', [0x74] = '6', [0x6C] = '7', [0x75] = '8', [0x7D] = '9',
    };

    return (code < sizeof(map)) ? map[code] : 0;
}
