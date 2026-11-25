/*
 * Point d'entree principal RP2350-PiZero (HDMI/DVI + Dual USB Host + PS/2 + SD + PSRAM)
 * Framework Arduino Earle Philhower avec main() direct (sans setup/loop)
 */

#include <Arduino.h>
#include "pico/stdlib.h"
#include "board_hw.h"
#include "bus.h"
#include "config.h"

static void boot_blink(void)
{
    pinMode(ORIC_PIN_LED, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(ORIC_PIN_LED, HIGH);
        delay(80);
        digitalWrite(ORIC_PIN_LED, LOW);
        delay(80);
    }
}

int main(void)
{
    /* 1. Lire JEDEC et corriger le bit QE sur Flash Puya avant passage a 252 MHz */
    board_flash_init();

    /* 2. Basculer a 252 MHz avec diviseur QMI Flash sur (63 MHz <= 66 MHz) */
    board_set_clock_252mhz();

    /* 3. Initialiser le runtime Arduino puis la PSRAM APS6404L sur CS1 (GPIO 47) */
    initVariant();
    board_psram_init(BOARD_PSRAM_CS_PIN);

    boot_blink();
    delay(150);

    busInit();
    digitalWrite(ORIC_PIN_LED, HIGH);
    delay(60);
    digitalWrite(ORIC_PIN_LED, LOW);
    busMainLoop();

    return 0;
}
