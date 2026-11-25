/*
 * Oric Atmos Emulator for Waveshare RP2350-PiZero (HDMI/DVI)
 * Configuration materielle, carte memoire Oric et brochage RP2350B
 */

#ifndef ORIC_CONFIG_H
#define ORIC_CONFIG_H

/* --------------------------------------------------------------------------
 * Horloge et memoire Oric Atmos
 * -------------------------------------------------------------------------- */
#define ORIC_CLOCK_FREQ         1000000UL
#define ORIC_AY38910_CLOCK      1000000UL

#define ORIC_RAM_START          0x0000U
#define ORIC_RAM_SIZE           0xC000U   /* 48 Ko RAM : 0x0000 - 0xBFFF */
#define ORIC_ROM_START          0xC000U
#define ORIC_ROM_SIZE           0x4000U   /* 16 Ko ROM : 0xC000 - 0xFFFF */

#define ORIC_IO_START           0x0300U
#define ORIC_IO_END             0x0400U   /* Plage I/O complete 0x0300 - 0x03FF */
#define ORIC_ACIA_START         0x031CU
#define ORIC_ACIA_END           0x0320U   /* ACIA 6551 : 0x031C - 0x031F */

#define ORIC_SCREEN_ADDR        0xBB80U
#define ORIC_HIRES_ADDR         0xA000U
#define ORIC_SCREEN_COLS        40U
#define ORIC_SCREEN_ROWS        28U
#define ORIC_MONITOR_SIZE       (ORIC_SCREEN_COLS * ORIC_SCREEN_ROWS) /* 1120 octets */

#define ORIC_SCREEN_WIDTH       240U
#define ORIC_SCREEN_HEIGHT      224U

/* --------------------------------------------------------------------------
 * Backend audio unifie (HDMI 48 kHz, I2S 48 kHz et PWM 96 kHz commutables via F4)
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * Brochage Waveshare RP2350-PiZero (RP2350B QFN-80)
 * -------------------------------------------------------------------------- */
/* Mini-HDMI / DVI integre sur PCB (PIO0, gpio_base=16) */
#define PIN_DVI_D2              32U  /* GPIO 32/33 : TMDS D2 (Rouge) */
#define PIN_DVI_D1              34U  /* GPIO 34/35 : TMDS D1 (Vert) */
#define PIN_DVI_D0              36U  /* GPIO 36/37 : TMDS D0 (Bleu + Synchro) */
#define PIN_DVI_CLK             38U  /* GPIO 38/39 : TMDS Clock */

/* Port PIO-USB Type-C integre sur PCB (PIO1) */
#define PIN_USB_HOST_DP         28U  /* GPIO 28 : D+, GPIO 29 : D- */

/* Lecteur MicroSD / TF integre sur PCB (SPI1 hardware + CS GPIO43) */
#define PIN_SD_SCK              30U
#define PIN_SD_MOSI             31U
#define PIN_SD_MISO             40U
#define PIN_SD_CS               43U

/* Port header 40 broches : Bus I2C1 (Broches 3=SDA, 5=SCL) */
#define PIN_I2C_SDA             2U
#define PIN_I2C_SCL             3U

/* Port header 40 broches : UART0 ACIA 6551 (Broches 8=TX, 10=RX) */
#define PIN_UART_TX             14U
#define PIN_UART_RX             15U

/* Port header 40 broches : Clavier PS/2 (PIO2 SM0, CLOCK == DATA + 1) */
#define PIN_PS2_DATA            16U  /* Broche 36 */
#define PIN_PS2_CLOCK           17U  /* Broche 11 */

/* Port header 40 broches : Manette NES (Port 1 compatible pico-infonesPlus) */
#define PIN_NES_CLOCK           5U   /* Broche 29 */
#define PIN_NES_DATA            6U   /* Broche 31 */
#define PIN_NES_LATCH           9U   /* Broche 21 */

/* Port header 40 broches : Audio I2S (PIO2 SM1, LRCLK == BCLK + 1, std HAT) */
#define PIN_I2S_BCLK            18U  /* Broche 12 */
#define PIN_I2S_LRCLK           19U  /* Broche 35 */
#define PIN_I2S_DOUT            21U  /* Broche 40 */

/* Port header 40 broches : Audio PWM (Slice 5 Ch A, Broche 37) */
#define PIN_AUDIO_PWM           26U

/* LED d'activite optionnelle (Broche 22 du header 40 broches) */
#define ORIC_PIN_LED            25U

/* Audio : 48000 Hz exactement verrouille sur 25.2 MHz HDMI (N=6144, CTS=25200) */
#define SAMPLE_FREQUENCY        48000U

/* Limites stockage SD et mapping manette NES */
#define SD_MAX_FILENAME_LEN     48U
#define SD_MAX_LIST_FILES       64U
#define NES_CSV_ROWS            8U
#define NES_CSV_COLS            3U

#endif /* ORIC_CONFIG_H */
