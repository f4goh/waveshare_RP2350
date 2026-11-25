# Émulateur Apple IIe (MII) pour Waveshare RP2350-PiZero

Émulateur complet de l'**Apple IIe** (basé sur le cœur `MII` 65C02 de `frank-apple`), adapté pour la carte **Waveshare RP2350-PiZero** (RP2350B @ 252 MHz, 16 Mo Flash QSPI, 8 Mo PSRAM APS6404L) avec sortie vidéo **HDMI/DVI 640×480p60** et **audio unifié 3-en-1 (HDMI / I2S / PWM)**.

---

## 1. Caractéristiques principales

- **CPU & Mémoire** : Apple IIe complet avec processeur **65C02** (`1,023 MHz`), `128 Ko` RAM (`64 Ko` Main + `64 Ko` Aux), ROM Apple IIe Enhanced (`16 Ko`) + Video ROM (`4 Ko`).
- **Modes vidéo Apple IIe** (`320×240` natif doublé 2× en `640×480p @ 60 Hz` HDMI/DVI) :
  - `Text` (40/80 colonnes), `Lo-Res`, `Hi-Res`, `Double Hi-Res`, mode `Mixed`.
  - Palettes sélectionnables à chaud dans le menu `F4` : **Couleur NTSC**, **Vert Monochrome (P31)**, **Ambre Monochrome (P34)**.
- **Stockage disquettes & disques durs (carte MicroSD FAT32)** :
  - Contrôleur **Disk II** (Slot 6) et **SmartPort** (Slot 5).
  - Formats supportés : **`.DSK`**, **`.NIB`**, **`.WOZ`**, **`.BDSK`** (avec support de réécriture / *write-back* sur les fichiers `.bdsk` en PSRAM).
- **Audio unifié 3-en-1** (commutable en temps réel dans le menu `F4`, `48 kHz`) :
  1. **HDMI Audio (48 kHz L-PCM)** : Data Islands TERC4/BCH4 sur le câble HDMI (`N=6144`, `CTS=25200`).
  2. **I2S DAC (PCM5102)** : sortie numérique `GPIO 18/19/21`.
  3. **PWM Audio (96 kHz)** : sortie analogique filtrée sur `GPIO 26`.
- **Entrées** :
  - **Clavier USB Dual-Host** (USB-OTG natif + PIO-USB `GPIO 28/29`, supporte hubs USB, claviers et adaptateurs 2.4 GHz).
  - **Clavier PS/2** direct (`PIO2`, `GPIO 16/17`).
  - **Manette NES** sur connecteur GPIO (`GPIO 5/6/9`) -> émule le joystick/paddles Apple II (`$C064`/`$C065`) et les boutons `Open Apple` / `Closed Apple` (`$C061`/`$C062`).

---

## 2. Affectation des GPIO (identique à Oric & C64)

| Périphérique | GPIO RP2350 | Broche Header 40 pts | Détails |
| :--- | :--- | :--- | :--- |
| **Mini-HDMI / DVI** | `32/33` (D2), `34/35` (D1), `36/37` (D0), `38/39` (CLK) | Interne PCB | `PIO0` (base 16), 640×480p60 + audio L-PCM 48 kHz |
| **USB Type-C (PIO-USB)** | `28` (D+), `29` (D-) | Interne PCB | `PIO1` Host (Port 0 natif + Port 1 PIO-USB) |
| **MicroSD (SPI1)** | `30` (SCK), `31` (MOSI), `40` (MISO), `43` (CS) | Interne PCB | Carte MicroSD FAT32 |
| **PSRAM 8 Mo** | `47` (CS1) | Interne PCB | APS6404L mappée en XIP `0x11000000` |
| **Manette NES** | `5` (CLK), `6` (DATA), `9` (LATCH) | **29, 31, 21** | Alimentation **3.3V** (broche 1) + GND |
| **Clavier PS/2** | `16` (DATA), `17` (CLOCK) | **36, 11** | Pull-ups `4.7 kΩ` vers 3.3V recommandés |
| **Audio I2S (PCM5102)** | `18` (BCK), `19` (LRCK), `21` (DIN) | **12, 35, 40** | `48 kHz` (`PIO2 SM1`) |
| **Audio PWM** | `26` (PWM) | **37** | `96 kHz` (avec filtre RC) |
| **UART0** | `14` (TX), `15` (RX) | **8, 10** | `115200` bauds 8N1 |

---

## 3. Raccourcis Clavier, Joystick & Manettes

| Touche | Action |
| :--- | :--- |
| **F4** | Ouvre le **Menu de Configuration OSD** (Audio HDMI/I2S/PWM, Volume, Moniteur Couleur/Vert/Ambre, Reset). |
| **F9** | Affiche / masque la **vitesse CPU** (`kHz + %`) en haut de l'écran. |
| **F11** *(ou **Select** manette)* | Ouvre le **Sélecteur de Disquettes MicroSD** (`.DSK`, `.NIB`, `.WOZ`, `.BDSK`). |
| **F12** *(maintien)* | Débloque la vitesse CPU tant que la touche est maintenue. |
| **ScrLock** *(bascule)* | Active / désactive le déblocage de la vitesse CPU. |
| **Ctrl+Alt+Suppr** *(ou **F2** / **Ctrl+F2**)* | Warm Reset (`Ctrl+Reset`) / Cold Reset (redémarrage à froid). |
| **Alt Gauche / Win Gauche** | Bouton **Open Apple** (Paddle 0, `$C061`). |
| **Alt Droit / Win Droit** | Bouton **Closed Apple** (Paddle 1, `$C062`). |
| **Flèches directionnelles** | Touches curseurs Apple IIe (`0x08`, `0x15`, `0x0A`, `0x0B`). |

### Émulation Joystick au Clavier (`KP` / Flèches) & Manettes (NES / USB HID)

- **Pavé numérique (`KP`) & Flèches classiques** : `KP 8` / `↑` = Haut, `KP 2` / `KP 5` / `↓` = Bas, `KP 4` / `←` = Gauche, `KP 6` / `→` = Droite, `Ctrl` = **A** (Open Apple), `Alt` = **B** (Closed Apple), `Inser` = **Start**, `Suppr` = **Select**.
- **Manette NES (GPIO) & Manette USB HID** : D-Pad = Joystick (`$C064`/`$C065`), **A** = Open Apple (`$C061`), **B** = Closed Apple (`$C062`), **Select** = Sélecteur de disquettes (`F11`), **Start+A+B** = Reset.

---

## 4. Compilation et Flashage (PlatformIO)

```bash
cd applenew
pio run
```

Le binaire UF2 compilé est copié sous [apple_rp2350_pizero.uf2](file:///home/alecren/Documents/oric/applenew/apple_rp2350_pizero.uf2).
Maintenez **BOOT** en branchant la carte en USB puis glissez-déposez `apple_rp2350_pizero.uf2` sur le lecteur `RP2350`.
