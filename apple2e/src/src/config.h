/*
 * Apple IIe Emulator (MII) for Waveshare RP2350-PiZero (HDMI/DVI)
 * Hardware Configuration & RP2350B Pinout (identical to Oric & C64)
 */

#ifndef APPLE_WS_CONFIG_H
#define APPLE_WS_CONFIG_H

/* System Clocks: 252 MHz exact 10x multiple of 25.2 MHz VGA/HDMI pixel clock */
#ifndef APPLE_SYS_CLK_KHZ
#define APPLE_SYS_CLK_KHZ       252000
#endif
#ifndef APPLE_SYS_CLK_HZ
#define APPLE_SYS_CLK_HZ        252000000UL
#endif

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
#define SD_SPI_PORT             spi1
#define PIN_SD_SCK              30U
#define PIN_SD_MOSI             31U
#define PIN_SD_MISO             40U
#define PIN_SD_CS               43U

/* Onboard 8MB APS6404L QSPI PSRAM */
#define PIN_PSRAM_CS            47U
#define PSRAM_XIP_CACHED_BASE   0x11000000UL
#define PSRAM_XIP_UNCACHED_BASE 0x15000000UL

/* Port header 40 broches : Bus I2C1 (Broches 3=SDA, 5=SCL) */
#define PIN_I2C_SDA             2U
#define PIN_I2C_SCL             3U

/* Port header 40 broches : UART0 (Broches 8=TX, 10=RX) */
#define PIN_UART_TX             14U
#define PIN_UART_RX             15U
#define APPLE_UART_ID           uart0
#define APPLE_UART_BAUD         115200

/* Port header 40 broches : Clavier PS/2 (PIO2 SM0, CLOCK == DATA + 1) */
#define PIN_PS2_DATA            16U  /* Broche 36 */
#define PIN_PS2_CLOCK           17U  /* Broche 11 */

/* Port header 40 broches : Manette NES */
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
#define APPLE_PIN_LED           25U

/* Audio : 48000 Hz exactement verrouille sur 25.2 MHz HDMI (N=6144, CTS=25200) */
#define SAMPLE_FREQUENCY        48000U
#define AUDIO_SAMPLE_RATE       48000U

/* Apple IIe Display Geometry (320x240 -> 640x480 HDMI Fullscreen) */
#define APPLE_FB_WIDTH          320
#define APPLE_FB_HEIGHT         240

#endif /* APPLE_WS_CONFIG_H */
