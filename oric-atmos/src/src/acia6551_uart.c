/*
 * Backend UART0 RP2040 pour ACIA 6551 (FIFO circulaire 512 octets O(1))
 */

#include "acia6551.h"
#include "config.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"

#define COM_FIFO_SIZE 512U
#define COM_FIFO_MASK (COM_FIFO_SIZE - 1U)

static uint8_t  s_fifoBuf[COM_FIFO_SIZE];
static uint16_t s_fifoHead  = 0;
static uint16_t s_fifoTail  = 0;
static bool     s_uartReady = false;

static uint32_t s_curBaud     = 0;
static int      s_curBits     = 0;
static char     s_curParity   = 0;
static int      s_curStopbits = 0;

static void com_poll_rx(void)
{
    if (!s_uartReady) {
        return;
    }
    while (uart_is_readable(uart0)) {
        uint8_t  c    = uart_getc(uart0);
        uint16_t next = (uint16_t)((s_fifoHead + 1u) & COM_FIFO_MASK);
        if (next != s_fifoTail) {
            s_fifoBuf[s_fifoHead] = c;
            s_fifoHead = next;
        }
    }
}

static uint8_t com_stat(uint8_t stat)
{
    return (uint8_t)(stat | ASTF_CARRIER | ASTF_DSR);
}

static bool com_has_byte(uint8_t *data)
{
    if (s_fifoHead == s_fifoTail) {
        return false;
    }
    *data = s_fifoBuf[s_fifoTail];
    return true;
}

static bool com_get_byte(uint8_t *data)
{
    if (s_fifoHead == s_fifoTail) {
        return false;
    }
    *data = s_fifoBuf[s_fifoTail];
    s_fifoTail = (uint16_t)((s_fifoTail + 1u) & COM_FIFO_MASK);
    return true;
}

static bool com_put_byte(uint8_t data)
{
    if (!s_uartReady) {
        return false;
    }
    uart_putc_raw(uart0, (char)data);
    return true;
}

static bool com_set_config(uint32_t baud, int bits, char parity, int stopbits)
{
    if (baud == 0) {
        return false;
    }
    if (s_uartReady && baud == s_curBaud && bits == s_curBits &&
        parity == s_curParity && stopbits == s_curStopbits) {
        return true;
    }

    uart_parity_t p = UART_PARITY_NONE;
    if (parity == 'E' || parity == 'e') {
        p = UART_PARITY_EVEN;
    } else if (parity == 'O' || parity == 'o') {
        p = UART_PARITY_ODD;
    }

    if (s_uartReady) {
        uart_set_baudrate(uart0, baud);
    } else {
        uart_init(uart0, baud);
        gpio_set_function(PIN_UART_TX, UART_FUNCSEL_NUM(uart0, PIN_UART_TX));
        gpio_set_function(PIN_UART_RX, UART_FUNCSEL_NUM(uart0, PIN_UART_RX));
        s_uartReady = true;
    }
    uart_set_format(uart0, (uint)bits, (uint)stopbits, p);

    s_curBaud     = baud;
    s_curBits     = bits;
    s_curParity   = parity;
    s_curStopbits = stopbits;
    return true;
}

static void com_done(struct acia *acia)
{
    if (s_uartReady) {
        uart_deinit(uart0);
        s_uartReady = false;
    }
    s_curBaud = 0;
    s_fifoHead = s_fifoTail = 0;
    acia_init_none(acia);
}

bool acia_init_com(struct acia *acia)
{
    s_fifoHead = s_fifoTail = 0;
    acia->done       = com_done;
    acia->stat       = com_stat;
    acia->has_byte   = com_has_byte;
    acia->get_byte   = com_get_byte;
    acia->put_byte   = com_put_byte;
    acia->set_config = com_set_config;
    acia->poll_rx    = com_poll_rx;

    return com_set_config(115200, 8, 'N', 1);
}
