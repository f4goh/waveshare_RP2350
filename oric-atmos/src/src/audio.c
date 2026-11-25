/*
 * Pilote Audio multi-backend unifie (commutable a chaud depuis le menu F4) :
 * - AUDIO_OUT_HDMI : HDMI Data Island L-PCM 48 kHz (Core 1 PIO0)
 * - AUDIO_OUT_I2S  : I2S 48 kHz 32 bits (64*fS) sur PIO2 + DMA_IRQ_1 (GPIO 18/19/21)
 * - AUDIO_OUT_PWM  : PWM 96 kHz sur GPIO 26 (Slice 5 Ch A, PWM_IRQ_WRAP_0)
 */

#include "audio.h"
#include "emu2149.h"
#include "hdmi_dvi.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

#include <string.h>

static PSG     *s_psg0       = NULL;
static uint8_t  s_psg0Reg    = 0;
static int32_t  s_dcFilter   = 0;
static uint64_t s_cycleAccum = 0;
static bool     s_mute       = false;
static uint16_t s_volScale   = 256u;
static bool     s_dcFilterOn = true;

#if defined(AUDIO_BACKEND_I2S)
static volatile uint8_t s_audioOutMode = (uint8_t)AUDIO_OUT_I2S;
#elif defined(AUDIO_BACKEND_PWM)
static volatile uint8_t s_audioOutMode = (uint8_t)AUDIO_OUT_PWM;
#else
static volatile uint8_t s_audioOutMode = (uint8_t)AUDIO_OUT_HDMI;
#endif

static bool s_hwInited = false;

void audioSetMute(bool mute)
{
    s_mute = mute;
}

void audioSetVolume(uint8_t percent)
{
    if (percent >= 100u) {
        s_volScale = 256u;
    } else if (percent <= 25u) {
        s_volScale = 64u;
    } else {
        s_volScale = (uint16_t)(((uint32_t)percent * 256u) / 100u);
    }
}

void audioSetDcFilter(bool enable)
{
    s_dcFilterOn = enable;
    if (!enable) {
        s_dcFilter = 0;
    }
}

uint8_t audioGetOutputMode(void)
{
    return s_audioOutMode;
}

/* --------------------------------------------------------------------------
 * Backend 0 : Audio HDMI 48 kHz (paquets de 4 echantillons L-PCM vers Core 1)
 * -------------------------------------------------------------------------- */
static int16_t s_hdmiPktL[4];
static int16_t s_hdmiPktR[4];
static uint8_t s_hdmiPktIdx = 0u;

static inline void __not_in_flash_func(hdmi_audio_push)(int16_t s)
{
    s_hdmiPktL[s_hdmiPktIdx] = s;
    s_hdmiPktR[s_hdmiPktIdx] = s;
    s_hdmiPktIdx++;
    if (s_hdmiPktIdx >= 4u) {
        s_hdmiPktIdx = 0u;
        hdmi_dvi_push_audio_4samples(s_hdmiPktL, s_hdmiPktR);
    }
}

/* --------------------------------------------------------------------------
 * Ring buffer SPSC Core 0 -> ISR pour I2S et PWM
 * -------------------------------------------------------------------------- */
#define PCM_RING_SIZE 1024U
#define PCM_RING_MASK (PCM_RING_SIZE - 1U)

static volatile int16_t  s_pcmRing[PCM_RING_SIZE];
static volatile uint16_t s_pcmHead = 0;
static volatile uint16_t s_pcmTail = 0;

static inline void __not_in_flash_func(pcm_ring_push)(int16_t s)
{
    uint16_t head = s_pcmHead;
    uint16_t next = (uint16_t)((head + 1u) & PCM_RING_MASK);
    if (next != s_pcmTail) {
        s_pcmRing[head] = s;
        __dmb();
        s_pcmHead = next;
    }
}

static inline bool __not_in_flash_func(pcm_ring_pop)(int16_t *out)
{
    uint16_t tail = s_pcmTail;
    if (tail == s_pcmHead) {
        return false;
    }
    *out = s_pcmRing[tail];
    __dmb();
    s_pcmTail = (uint16_t)((tail + 1u) & PCM_RING_MASK);
    return true;
}

/* --------------------------------------------------------------------------
 * Backend 2 : PWM 96 kHz sur GPIO 26 (Slice 5 Ch A, PWM_IRQ_WRAP_0)
 * -------------------------------------------------------------------------- */
#define PWM_AUDIO_WRAP   2624U
#define PWM_AUDIO_CENTER 1312

static uint             s_pwmSlice = 0;
static uint             s_pwmChan  = 0;
static volatile int16_t s_pwmPrev  = 0;
static volatile int16_t s_pwmCur   = 0;
static volatile uint8_t s_pwmPhase = 0;

static void __not_in_flash_func(pwm_wrap_irq)(void)
{
    pwm_clear_irq(s_pwmSlice);

    int32_t sample;
    if ((s_pwmPhase & 1u) == 0u) {
        int16_t next_s = 0;
        if (!pcm_ring_pop(&next_s)) {
            next_s = (int16_t)((s_pwmCur * 15) / 16);
        }
        s_pwmPrev = s_pwmCur;
        s_pwmCur  = next_s;
        sample    = ((int32_t)s_pwmPrev + (int32_t)s_pwmCur) >> 1;
    } else {
        sample = (int32_t)s_pwmCur;
    }
    s_pwmPhase ^= 1u;

    int32_t level = PWM_AUDIO_CENTER + ((sample * 1200) >> 15);
    if (level < 0)                        level = 0;
    if (level > (int32_t)PWM_AUDIO_WRAP)  level = (int32_t)PWM_AUDIO_WRAP;
    pwm_set_chan_level(s_pwmSlice, s_pwmChan, (uint16_t)level);
}

static void pwm_backend_init(void)
{
    s_pwmPrev  = s_pwmCur = 0;
    s_pwmPhase = 0;

    gpio_set_function(PIN_AUDIO_PWM, GPIO_FUNC_PWM);
    s_pwmSlice = pwm_gpio_to_slice_num(PIN_AUDIO_PWM);
    s_pwmChan  = pwm_gpio_to_channel(PIN_AUDIO_PWM);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac(&cfg, 1, 0);
    pwm_config_set_wrap(&cfg, PWM_AUDIO_WRAP);
    pwm_init(s_pwmSlice, &cfg, false);
    pwm_set_chan_level(s_pwmSlice, s_pwmChan, PWM_AUDIO_CENTER);

    pwm_clear_irq(s_pwmSlice);
    pwm_set_irq0_enabled(s_pwmSlice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP_0, pwm_wrap_irq);
    irq_set_enabled(PWM_IRQ_WRAP_0, false);
}

/* --------------------------------------------------------------------------
 * Backend 1 : I2S 48 kHz 32 bits/canal (64*fS) sur PIO2 + DMA_IRQ_1
 * -------------------------------------------------------------------------- */
#define I2S_PIO          pio2
#define I2S_DMA_FRAMES   64U
#define I2S_DMA_WORDS    (I2S_DMA_FRAMES * 2U)

static const uint16_t s_i2sOutInsns[] = {
    0x7001, /* 0: out pins, 1       side 0b10 (bitloop1: R, LRCLK=1, BCLK=0) */
    0x1840, /* 1: jmp x--, 0        side 0b11 */
    0x6001, /* 2: out pins, 1       side 0b00 (LSB R + front LRCLK -> 0) */
    0xe83e, /* 3: set x, 30         side 0b01 */
    0x6001, /* 4: out pins, 1       side 0b00 (bitloop0: L, LRCLK=0, BCLK=0) */
    0x0844, /* 5: jmp x--, 4        side 0b01 */
    0x7001, /* 6: out pins, 1       side 0b10 (LSB L + front LRCLK -> 1) */
    0xf83e, /* 7: set x, 30         side 0b11 (entry_point) */
};

static const struct pio_program s_i2sOutProg = {
    .instructions = s_i2sOutInsns,
    .length       = 8,
    .origin       = -1,
    .pio_version  = 0,
#if defined(PICO_PIO_VERSION) && (PICO_PIO_VERSION > 0)
    .used_gpio_ranges = 0x0,
#endif
};

static uint32_t __aligned(4) s_i2sBuf[2][I2S_DMA_WORDS];
static uint     s_i2sSm = 0;
static uint     s_i2sDma[2];
static int16_t  s_i2sLast = 0;

static void __not_in_flash_func(fill_i2s_buf)(uint32_t *dst)
{
    int16_t cur = s_i2sLast;
    for (uint i = 0; i < I2S_DMA_FRAMES; i++) {
        if (!pcm_ring_pop(&cur)) {
            cur = (int16_t)((cur * 15) / 16);
        }
        uint32_t w = (uint32_t)((int32_t)cur << 16);
        dst[2u * i]      = w;
        dst[2u * i + 1u] = w;
    }
    s_i2sLast = cur;
}

static void __not_in_flash_func(i2s_dma_irq1)(void)
{
    uint32_t ints = dma_hw->ints1;
    for (uint idx = 0; idx < 2u; idx++) {
        uint32_t bit = 1u << s_i2sDma[idx];
        if (ints & bit) {
            dma_hw->ints1 = bit;
            fill_i2s_buf(s_i2sBuf[idx]);
            dma_channel_set_read_addr(s_i2sDma[idx], s_i2sBuf[idx], false);
            dma_channel_set_transfer_count(s_i2sDma[idx], I2S_DMA_WORDS, false);
        }
    }
}

static void i2s_backend_init(int sampleRate)
{
    s_i2sLast = 0;
    memset(s_i2sBuf, 0, sizeof(s_i2sBuf));

    int sm = pio_claim_unused_sm(I2S_PIO, true);
    s_i2sSm = (uint)sm;
    uint offset = (uint)pio_add_program(I2S_PIO, &s_i2sOutProg);

    pio_gpio_init(I2S_PIO, PIN_I2S_DOUT);
    pio_gpio_init(I2S_PIO, PIN_I2S_BCLK);
    pio_gpio_init(I2S_PIO, PIN_I2S_LRCLK);
    gpio_set_drive_strength(PIN_I2S_DOUT,  GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(PIN_I2S_BCLK,  GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(PIN_I2S_LRCLK, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_slew_rate(PIN_I2S_DOUT,  GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(PIN_I2S_BCLK,  GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(PIN_I2S_LRCLK, GPIO_SLEW_RATE_FAST);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + 7u);
    sm_config_set_sideset(&c, 2, false, false);
    sm_config_set_out_pins(&c, PIN_I2S_DOUT, 1);
    sm_config_set_sideset_pins(&c, PIN_I2S_BCLK);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    float pio_clk = (float)sampleRate * 32.0f * 2.0f * 2.0f;
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / pio_clk);

    pio_sm_init(I2S_PIO, s_i2sSm, offset, &c);
    uint64_t pin_mask = ((uint64_t)1u << PIN_I2S_DOUT) | ((uint64_t)3u << PIN_I2S_BCLK);
    pio_sm_set_pindirs_with_mask64(I2S_PIO, s_i2sSm, pin_mask, pin_mask);
    pio_sm_set_pins_with_mask64(I2S_PIO, s_i2sSm, 0u, pin_mask);
    pio_sm_exec(I2S_PIO, s_i2sSm, pio_encode_jmp(offset + 7u));

    s_i2sDma[0] = (uint)dma_claim_unused_channel(true);
    s_i2sDma[1] = (uint)dma_claim_unused_channel(true);

    for (uint idx = 0; idx < 2u; idx++) {
        dma_channel_config dc = dma_channel_get_default_config(s_i2sDma[idx]);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, true);
        channel_config_set_write_increment(&dc, false);
        channel_config_set_dreq(&dc, pio_get_dreq(I2S_PIO, s_i2sSm, true));
        channel_config_set_chain_to(&dc, s_i2sDma[idx ^ 1u]);
        dma_channel_configure(
            s_i2sDma[idx],
            &dc,
            &I2S_PIO->txf[s_i2sSm],
            s_i2sBuf[idx],
            I2S_DMA_WORDS,
            false);
    }

    uint32_t mask = (1u << s_i2sDma[0]) | (1u << s_i2sDma[1]);
    dma_hw->ints1 = mask;
    hw_set_bits(&dma_hw->inte1, mask);
    irq_set_exclusive_handler(DMA_IRQ_1, i2s_dma_irq1);
    irq_set_enabled(DMA_IRQ_1, true);

    pio_sm_set_enabled(I2S_PIO, s_i2sSm, false);
    dma_channel_start(s_i2sDma[0]);
}

/* --------------------------------------------------------------------------
 * Commutation a chaud entre HDMI, I2S et PWM
 * -------------------------------------------------------------------------- */
void audioSetOutputMode(uint8_t mode)
{
    if (mode > (uint8_t)AUDIO_OUT_PWM) {
        mode = (uint8_t)AUDIO_OUT_HDMI;
    }

    s_audioOutMode = mode;
    s_pcmHead      = 0u;
    s_pcmTail      = 0u;
    s_pwmPrev      = 0;
    s_pwmCur       = 0;
    s_i2sLast      = 0;
    s_hdmiPktIdx   = 0u;

    hdmi_dvi_set_audio_enabled(mode == (uint8_t)AUDIO_OUT_HDMI);

    if (!s_hwInited) {
        return;
    }

    /* Activer/desactiver PWM (GPIO 26 Slice 5) */
    bool en_pwm = (mode == (uint8_t)AUDIO_OUT_PWM);
    if (!en_pwm) {
        irq_set_enabled(PWM_IRQ_WRAP_0, false);
        pwm_set_enabled(s_pwmSlice, false);
        pwm_set_chan_level(s_pwmSlice, s_pwmChan, PWM_AUDIO_CENTER);
    } else {
        pwm_clear_irq(s_pwmSlice);
        irq_set_enabled(PWM_IRQ_WRAP_0, true);
        pwm_set_enabled(s_pwmSlice, true);
    }

    /* Activer/desactiver I2S (PIO2 SM -> gele automatiquement le DREQ DMA_IRQ_1) */
    bool en_i2s = (mode == (uint8_t)AUDIO_OUT_I2S);
    pio_sm_set_enabled(I2S_PIO, s_i2sSm, en_i2s);
}

/* --------------------------------------------------------------------------
 * API publique audio
 * -------------------------------------------------------------------------- */
void audioInit(int psgClock, int sampleRate)
{
    s_psg0 = PSG_new((uint32_t)psgClock, (uint32_t)sampleRate);
    PSG_setVolumeMode(s_psg0, EMU2149_VOL_AY_3_8910);
    PSG_reset(s_psg0);

    s_psg0Reg    = 0;
    s_dcFilter   = 0;
    s_cycleAccum = 0;

    pwm_backend_init();
    i2s_backend_init(sampleRate);
    s_hwInited = true;

    audioSetOutputMode(s_audioOutMode);
}

void audioReset(void)
{
    if (s_psg0) {
        PSG_reset(s_psg0);
    }
    s_psg0Reg    = 0;
    s_dcFilter   = 0;
    s_cycleAccum = 0;
    s_pcmHead    = 0;
    s_pcmTail    = 0;
    s_pwmPrev    = 0;
    s_pwmCur     = 0;
    s_i2sLast    = 0;
    s_hdmiPktIdx = 0u;
    hdmi_dvi_reset_audio_queue();
}

void __not_in_flash_func(audioStepCycles)(uint8_t cycles)
{
    if (!s_psg0) {
        return;
    }

    s_cycleAccum += (uint64_t)cycles * (uint64_t)SAMPLE_FREQUENCY;
    while (s_cycleAccum >= (uint64_t)ORIC_CLOCK_FREQ) {
        s_cycleAccum -= (uint64_t)ORIC_CLOCK_FREQ;

        int32_t raw = (int32_t)PSG_calc(s_psg0);
        int32_t centered;
        if (s_dcFilterOn) {
            s_dcFilter += (raw - s_dcFilter) >> 10;
            centered = raw - s_dcFilter;
        } else {
            centered = raw;
        }
        int32_t out = s_mute ? 0 : ((centered * 2 * (int32_t)s_volScale) >> 8);
        if (out > 32767)  out = 32767;
        if (out < -32768) out = -32768;

        if (s_audioOutMode == (uint8_t)AUDIO_OUT_HDMI) {
            hdmi_audio_push((int16_t)out);
        } else {
            pcm_ring_push((int16_t)out);
        }
    }
}

uint8_t __not_in_flash_func(audioReadPsg0)(void)
{
    return s_psg0 ? PSG_readReg(s_psg0, s_psg0Reg) : 0;
}

void __not_in_flash_func(audioWritePsg0)(uint16_t addr, uint8_t val)
{
    if (!s_psg0) {
        return;
    }
    if ((addr & 0x01u) == 0u) {
        s_psg0Reg = val;
    } else {
        PSG_writeReg(s_psg0, s_psg0Reg, val);
    }
}

uint8_t audioSize(void)
{
    return (uint8_t)sizeof(savePSG);
}

void audioSaveState(uint8_t *dst)
{
    if (s_psg0 && dst) {
        memcpy(dst, s_psg0, sizeof(savePSG));
    }
}

void audioLoadState(const uint8_t *src)
{
    if (s_psg0 && src) {
        memcpy(s_psg0, src, sizeof(savePSG));
    }
}
