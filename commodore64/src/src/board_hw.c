/*
 * Initialisation materielle bas niveau Waveshare RP2350-PiZero :
 *   - Correctif QE pour Flash Puya (0x85) en mode direct RAM-resident
 *   - Diviseur QMI transition/regime pour 252 MHz (SCK Flash = 63 MHz <= 66 MHz)
 *   - Initialisation QPI + retiming + sonde PSRAM APS6404L sur CS1 (GPIO 47)
 */

#include "board_hw.h"

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/structs/pads_qspi.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/sysinfo.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"

#include <string.h>

#define BOARD_PSRAM_NOCACHE_BASE 0x15000000UL

static void __no_inline_not_in_flash_func(board_flush_xip_cache)(void)
{
    rom_flash_flush_cache_fn flush_cache =
        (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
    flush_cache();
}

static uint8_t  s_flashJedec[4]  = {0};
static uint8_t  s_flashQeCode    = 0;
static uint8_t  s_flashQeDiag[6] = {0};
static uint8_t  s_psramCsPin     = BOARD_PSRAM_CS_PIN;
static uint8_t  s_psramMfid      = 0;
static uint8_t  s_psramKgd       = 0;
static uint32_t s_psramSizeBytes = 0;
static bool     s_psramTestOk    = false;

static void __no_inline_not_in_flash_func(qe_cmd_raw)(const uint8_t *tx, uint8_t *rxb, size_t n)
{
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS0N_BITS);
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);
    size_t txr = n;
    size_t rxr = n;
    while (txr || rxr) {
        uint32_t flags = qmi_hw->direct_csr;
        if (txr && !(flags & QMI_DIRECT_CSR_TXFULL_BITS)) {
            qmi_hw->direct_tx = *tx++;
            --txr;
        }
        if (rxr && !(flags & QMI_DIRECT_CSR_RXEMPTY_BITS)) {
            uint8_t b = (uint8_t)qmi_hw->direct_rx;
            if (rxb) {
                *rxb++ = b;
            }
            --rxr;
        }
    }
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS0N_BITS);
}

static uint8_t __no_inline_not_in_flash_func(qe_read_reg)(uint8_t cmd)
{
    uint8_t tx[2] = {cmd, 0u};
    uint8_t rxb[2] = {0u, 0u};
    qe_cmd_raw(tx, rxb, 2u);
    return rxb[1];
}

static void __no_inline_not_in_flash_func(flash_qe_fix_puya)(void)
{
    if (s_flashJedec[1] != 0x85u) {
        return;
    }

    uint32_t boot2_copy[64];
    const volatile uint32_t *b2 = (const volatile uint32_t *)BOOTRAM_BASE;
    for (int i = 0; i < 64; ++i) {
        boot2_copy[i] = b2[i];
    }
    __compiler_memory_barrier();

    rom_connect_internal_flash_fn connect_flash =
        (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
    rom_flash_exit_xip_fn exit_xip =
        (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
    rom_flash_flush_cache_fn flush_cache =
        (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);

    uint32_t m1_timing = qmi_hw->m[1].timing;
    uint32_t m1_rcmd   = qmi_hw->m[1].rcmd;
    uint32_t m1_rfmt   = qmi_hw->m[1].rfmt;
    uint32_t pads_save[count_of(pads_qspi_hw->io)];
    for (size_t i = 0; i < count_of(pads_qspi_hw->io); ++i) {
        pads_save[i] = pads_qspi_hw->io[i];
    }

    const uint32_t ints = save_and_disable_interrupts();
    connect_flash();
    exit_xip();

    /* Maintenir WP# (SD2) et HOLD# (SD3) a l'etat haut pendant le mode SPI simple */
    hw_write_masked(&pads_qspi_hw->io[3], PADS_QSPI_GPIO_QSPI_SD2_PUE_BITS,
                    PADS_QSPI_GPIO_QSPI_SD2_PUE_BITS | PADS_QSPI_GPIO_QSPI_SD2_PDE_BITS);
    hw_write_masked(&pads_qspi_hw->io[4], PADS_QSPI_GPIO_QSPI_SD3_PUE_BITS,
                    PADS_QSPI_GPIO_QSPI_SD3_PUE_BITS | PADS_QSPI_GPIO_QSPI_SD3_PDE_BITS);

    uint8_t jtx[4] = {0x9Fu, 0u, 0u, 0u};
    uint8_t jrx[4] = {0u};
    qe_cmd_raw(jtx, jrx, 4u);
    s_flashQeDiag[0] = jrx[1];
    s_flashQeDiag[1] = qe_read_reg(0x05u);
    uint8_t sr2 = qe_read_reg(0x35u);
    s_flashQeDiag[2] = sr2;

    if (jrx[1] != s_flashJedec[1] || jrx[2] != s_flashJedec[2] || jrx[3] != s_flashJedec[3]) {
        s_flashQeCode = 6u;
    } else if (sr2 & 0x02u) {
        s_flashQeCode = 1u;
    } else {
        uint8_t wr31[2] = {0x31u, (uint8_t)(sr2 | 0x02u)};
        const uint8_t c50 = 0x50u;
        const uint8_t c06 = 0x06u;

        qe_cmd_raw(&c50, NULL, 1u);
        qe_cmd_raw(wr31, NULL, 2u);
        for (volatile int i = 0; i < 2000; ++i) {
        }
        s_flashQeDiag[3] = qe_read_reg(0x35u);

        if (s_flashQeDiag[3] & 0x02u) {
            s_flashQeCode = 2u;
        } else {
            qe_cmd_raw(&c06, NULL, 1u);
            s_flashQeDiag[4] = qe_read_reg(0x05u);
            if (s_flashQeDiag[4] & 0x02u) {
                qe_cmd_raw(wr31, NULL, 2u);
                for (int i = 0; i < 20000; ++i) {
                    if (!(qe_read_reg(0x05u) & 0x01u)) {
                        break;
                    }
                }
                s_flashQeCode = (qe_read_reg(0x35u) & 0x02u) ? 3u : 4u;
                if (s_flashQeCode == 4u) {
                    uint8_t wr01[3] = {0x01u, (uint8_t)(qe_read_reg(0x05u) & ~0x03u), (uint8_t)(sr2 | 0x02u)};
                    qe_cmd_raw(&c06, NULL, 1u);
                    qe_cmd_raw(wr01, NULL, 3u);
                    for (int i = 0; i < 20000; ++i) {
                        if (!(qe_read_reg(0x05u) & 0x01u)) {
                            break;
                        }
                    }
                    s_flashQeCode = (qe_read_reg(0x35u) & 0x02u) ? 5u : 4u;
                }
            } else {
                s_flashQeCode = 4u;
            }
        }
    }
    s_flashQeDiag[5] = qe_read_reg(0x35u);

    flush_cache();
    ((void (*)(void))((uintptr_t)boot2_copy + 1u))();
    qmi_hw->m[1].timing = m1_timing;
    qmi_hw->m[1].rcmd   = m1_rcmd;
    qmi_hw->m[1].rfmt   = m1_rfmt;
    for (size_t i = 0; i < count_of(pads_qspi_hw->io); ++i) {
        pads_qspi_hw->io[i] = pads_save[i];
    }
    restore_interrupts(ints);
}

static inline void flash_timing_write(int divisor, int rxdelay)
{
    qmi_hw->m[0].timing = 0x60007000u |
                          ((uint32_t)rxdelay << QMI_M0_TIMING_RXDELAY_LSB) |
                          ((uint32_t)divisor << QMI_M0_TIMING_CLKDIV_LSB);
}

static void __no_inline_not_in_flash_func(flash_timings_apply)(int mhz)
{
    int divisor = 2, rxdelay = 2;
    board_compute_qmi_timing((uint32_t)mhz * 1000000u, BOARD_FLASH_MAX_MHZ * 1000000u, &divisor, &rxdelay);
    flash_timing_write(divisor, rxdelay);
}

static void __no_inline_not_in_flash_func(flash_timings_transition)(int from_mhz, int to_mhz)
{
    int hi = (from_mhz > to_mhz) ? from_mhz : to_mhz;
    int divisor = 4, rxdelay = 4;
    board_compute_qmi_timing((uint32_t)hi * 1000000u, BOARD_FLASH_MAX_MHZ * 1000000u, &divisor, &rxdelay);
    divisor *= 2;
    int max_div = (int)(QMI_M0_TIMING_CLKDIV_BITS >> QMI_M0_TIMING_CLKDIV_LSB);
    if (divisor > max_div) {
        divisor = max_div;
    }
    flash_timing_write(divisor, rxdelay);
}

void board_flash_init(void)
{
    if (s_flashJedec[0] == 0u) {
        uint8_t tx[4] = {0x9Fu, 0u, 0u, 0u};
        flash_do_cmd(tx, s_flashJedec, 4u);
    }
    flash_qe_fix_puya();
}

void __no_inline_not_in_flash_func(board_set_clock_252mhz)(void)
{
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(5);

    const int boot_mhz = (int)(clock_get_hz(clk_sys) / 1000000u);
    const uint32_t ints = save_and_disable_interrupts();
    flash_timings_transition(boot_mhz, 252);
    set_sys_clock_khz(252000u, true);
    flash_timings_apply(252);
    board_flush_xip_cache();
    restore_interrupts(ints);
}

static void __no_inline_not_in_flash_func(psram_retiming)(void)
{
    const uint32_t clock_hz = clock_get_hz(clk_sys);
    int divisor = 2, rxdelay = 2;
    board_compute_qmi_timing(clock_hz, BOARD_PSRAM_MAX_MHZ * 1000000u, &divisor, &rxdelay);
    const uint64_t clock_period_fs = 1000000000000000ULL / (uint64_t)clock_hz;
    int max_select = (int)((125ULL * 1000000ULL) / clock_period_fs);
    if (max_select > 63) {
        max_select = 63;
    }
    int min_deselect = (int)((18ULL * 1000000ULL + (clock_period_fs - 1ULL)) / clock_period_fs) -
                       (divisor + 1) / 2;
    if (min_deselect < 0) {
        min_deselect = 0;
    }
    qmi_hw->m[1].timing = (1u << QMI_M1_TIMING_COOLDOWN_LSB) |
                          ((uint32_t)QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB) |
                          ((uint32_t)max_select << QMI_M1_TIMING_MAX_SELECT_LSB) |
                          ((uint32_t)min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB) |
                          ((uint32_t)rxdelay << QMI_M1_TIMING_RXDELAY_LSB) |
                          ((uint32_t)divisor << QMI_M1_TIMING_CLKDIV_LSB);
}

static bool __no_inline_not_in_flash_func(psram_detect_raw)(uint8_t *mfid_out, uint8_t *kgd_out)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        /* Quitter QPI au cas ou la PSRAM etait restee en QPI apres reset a chaud */
        qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
        qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS |
                            ((uint32_t)QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB) |
                            0xF5u;
        while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
        }
        (void)qmi_hw->direct_rx;
        qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
        for (volatile int d = 0; d < 64; ++d) {
        }

        /* Read ID (0x9F + 3 adresses + MFID + KGD) */
        qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
        uint8_t mfid = 0u, kgd = 0u;
        for (int i = 0; i < 6; ++i) {
            qmi_hw->direct_tx = (i == 0) ? 0x9Fu : 0xFFu;
            while (!(qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS)) {
            }
            while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
            }
            uint8_t b = (uint8_t)qmi_hw->direct_rx;
            if (i == 4) {
                mfid = b;
            } else if (i == 5) {
                kgd = b;
            }
        }
        qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
        for (volatile int d = 0; d < 64; ++d) {
        }

        *mfid_out = mfid;
        *kgd_out  = kgd;
        if (kgd == 0x5Du || mfid == 0x5Du) {
            return true;
        }
    }
    return false;
}

static uint32_t __no_inline_not_in_flash_func(psram_probe_size_ram)(void)
{
    volatile uint8_t *p = (volatile uint8_t *)BOARD_PSRAM_BASE;
    const uint32_t mb1  = 1UL << 20;
    const uint32_t mb4  = 4UL << 20;
    const uint32_t mb8  = 8UL << 20;
    const uint32_t mb16 = 16UL << 20;

    for (uint32_t i = mb8; i < mb16; i += 4096u) p[i] = 16u;
    for (uint32_t i = mb4; i < mb8;  i += 4096u) p[i] = 8u;
    for (uint32_t i = mb1; i < mb4;  i += 4096u) p[i] = 4u;
    for (uint32_t i = 0u;  i < mb1;  i += 4096u) p[i] = 1u;

    uint8_t res = p[mb16 - 4096u];
    for (uint32_t i = mb16 - mb1; i < mb16; i += 4096u) {
        if (p[i] != res) {
            return 0u;
        }
    }
    if (res != 1u && res != 4u && res != 8u && res != 16u) {
        return 0u;
    }
    return (uint32_t)res << 20;
}

bool board_psram_quick_test(void)
{
    if (s_psramSizeBytes < 4096u) {
        s_psramTestOk = false;
        return false;
    }

    static const uint32_t kPatterns[4] = {
        0xA55A1234UL, 0x5AA5FEDCUL, 0x13579BDFUL, 0x2468ACE0UL
    };
    const uint32_t offsets[3] = {
        0u,
        s_psramSizeBytes / 2u,
        s_psramSizeBytes - 64u
    };

    volatile uint32_t *base = (volatile uint32_t *)BOARD_PSRAM_NOCACHE_BASE;
    bool ok = true;

    for (int k = 0; k < 3; ++k) {
        uint32_t idx = offsets[k] >> 2;
        uint32_t saved[4];
        for (int j = 0; j < 4; ++j) {
            saved[j] = base[idx + (uint32_t)j];
            base[idx + (uint32_t)j] = kPatterns[j] ^ ((uint32_t)k * 0x11111111UL);
        }
        board_flush_xip_cache();
        for (int j = 0; j < 4; ++j) {
            uint32_t expected = kPatterns[j] ^ ((uint32_t)k * 0x11111111UL);
            if (base[idx + (uint32_t)j] != expected) {
                ok = false;
            }
        }
        for (int j = 0; j < 4; ++j) {
            base[idx + (uint32_t)j] = saved[j];
        }
    }
    board_flush_xip_cache();

    s_psramTestOk = ok;
    return ok;
}

uint32_t __no_inline_not_in_flash_func(board_psram_init)(uint8_t cs_pin)
{
    s_psramCsPin = cs_pin;
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

    const uint32_t ints = save_and_disable_interrupts();
    qmi_hw->direct_csr = (30u << QMI_DIRECT_CSR_CLKDIV_LSB) | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
    }

    if (!psram_detect_raw(&s_psramMfid, &s_psramKgd)) {
        qmi_hw->direct_csr = 0u;
        restore_interrupts(ints);
        s_psramSizeBytes = 0u;
        s_psramTestOk    = false;
        return 0u;
    }

    qmi_hw->direct_csr = (10u << QMI_DIRECT_CSR_CLKDIV_LSB) |
                         QMI_DIRECT_CSR_EN_BITS |
                         QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
    }

    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | 0x35u; /* Enter QPI */
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
    }

    qmi_hw->direct_csr = 0u;
    restore_interrupts(ints);

    psram_retiming();

    qmi_hw->m[1].rfmt =
        ((uint32_t)QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB) |
        ((uint32_t)QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q   << QMI_M0_RFMT_ADDR_WIDTH_LSB)   |
        ((uint32_t)QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB) |
        ((uint32_t)QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q  << QMI_M0_RFMT_DUMMY_WIDTH_LSB)  |
        ((uint32_t)QMI_M0_RFMT_DATA_WIDTH_VALUE_Q   << QMI_M0_RFMT_DATA_WIDTH_LSB)   |
        ((uint32_t)QMI_M0_RFMT_PREFIX_LEN_VALUE_8   << QMI_M0_RFMT_PREFIX_LEN_LSB)   |
        (6u                                         << QMI_M0_RFMT_DUMMY_LEN_LSB);
    qmi_hw->m[1].rcmd = 0xEBu;

    qmi_hw->m[1].wfmt =
        ((uint32_t)QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB) |
        ((uint32_t)QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q   << QMI_M0_WFMT_ADDR_WIDTH_LSB)   |
        ((uint32_t)QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB) |
        ((uint32_t)QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q  << QMI_M0_WFMT_DUMMY_WIDTH_LSB)  |
        ((uint32_t)QMI_M0_WFMT_DATA_WIDTH_VALUE_Q   << QMI_M0_WFMT_DATA_WIDTH_LSB)   |
        ((uint32_t)QMI_M0_WFMT_PREFIX_LEN_VALUE_8   << QMI_M0_WFMT_PREFIX_LEN_LSB);
    qmi_hw->m[1].wcmd = 0x38u;

    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);

    s_psramSizeBytes = psram_probe_size_ram();
    if (s_psramSizeBytes > 0u) {
        board_psram_quick_test();
    }
    return s_psramSizeBytes;
}

uint32_t board_psram_size(void)
{
    return s_psramSizeBytes;
}

void board_hw_get_info(BoardHwInfo *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    uint32_t pkg = (*(volatile uint32_t *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET)) & 1u;
    out->is_rp2350b       = (pkg == 0u);
    out->sys_clock_hz     = clock_get_hz(clk_sys);
    memcpy(out->flash_jedec, s_flashJedec, 4u);
    out->flash_size_bytes = (s_flashJedec[3] < 31u && s_flashJedec[3] >= 12u)
                                ? (1UL << s_flashJedec[3])
                                : 0u;
    out->flash_qe_code    = s_flashQeCode;
    memcpy(out->flash_qe_diag, s_flashQeDiag, 6u);
    out->psram_cs_pin     = s_psramCsPin;
    out->psram_mfid       = s_psramMfid;
    out->psram_kgd        = s_psramKgd;
    out->psram_size_bytes = s_psramSizeBytes;
    out->psram_test_ok    = s_psramTestOk;
    out->qmi_m0_timing    = qmi_hw->m[0].timing;
    out->qmi_m1_timing    = qmi_hw->m[1].timing;
}

void board_hw_init(void)
{
    board_flash_init();
    board_set_clock_252mhz();
    board_psram_init(BOARD_PSRAM_CS_PIN);
}
