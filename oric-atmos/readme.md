# Émulateur Oric Atmos HDMI/DVI pour Waveshare RP2350-PiZero (`versionhdmi`)

Portage bare-metal / Arduino (`earlephilhower`, `int main(void)` direct sans `setup()`/`loop()`) de l'émulateur Oric Atmos pour la carte **Waveshare RP2350-PiZero** (RP2350B QFN-80, 16 Mo Flash QSPI, 8 Mo PSRAM APS6404L soudée sur `GPIO 47`, sortie Mini-HDMI/DVI, port USB natif + port PIO-USB, lecteur MicroSD intégré et connecteur GPIO 40 broches format Raspberry Pi).

---

## 1. Firmware unique multi-audio (`oric_rp2350_pizero.uf2`)

Un **seul binaire `.uf2` unifié** est généré à la racine de `versionhdmi/` : **[`oric_rp2350_pizero.uf2`](oric_rp2350_pizero.uf2)**.
Il embarque simultanément les **3 moteurs de sortie audio** (`HDMI`, `I2S` et `PWM`), commutables **à chaud** depuis le menu de configuration **`F4` → onglet `[2:CFG]` (`REGLAGES`) → `Sortie Audio`** (et sauvegardés dans `/oric.cfg` via la clé `audio_out=0/1/2`) :

| Mode sélectionnable dans `F4` | Clé `/oric.cfg` | Sortie Audio active | Sortie Vidéo | Ressources matérielles |
| :--- | :---: | :--- | :--- | :--- |
| **`<HDMI 48kHz>`** *(défaut)* | `audio_out=0` | **HDMI Data Island L-PCM 48,000 kHz** sur le câble Mini-HDMI | HDMI 1.4b 640×480p @ 60 Hz | Data Islands TERC4/BCH en SRAM sur Core 1 (`PIO0`, ratio exact `32/84` = `200` paquets/trame) |
| **`<I2S PCM5102>`** | `audio_out=1` | **I2S 48 kHz 32 bits (`64·fS`)** sur `GPIO 18/19/21` | DVI pur 640×480p @ 60 Hz (sans Data Islands) | `PIO2` + DMA ping-pong (`DMA_IRQ_1`) |
| **`<PWM GPIO26>`** | `audio_out=2` | **PWM 96 kHz** sur `GPIO 26` | DVI pur 640×480p @ 60 Hz (sans Data Islands) | PWM Slice 5 Ch A (`PWM_IRQ_WRAP_0`) |

> **Flashage** : maintenez le bouton `BOOT` au branchement USB sur le port **USB** de la carte, puis copiez `oric_rp2350_pizero.uf2` sur le lecteur `RP2350`.

---

## 2. Architecture et fonctionnement du programme

Le RP2350B fonctionne à **252 MHz** (`clk_sys` verrouillé exactement à 10× l'horloge pixel DVI 25,2 MHz). L'initialisation bas niveau dans [`src/main.c`](src/main.c) et [`src/board_hw.c`](src/board_hw.c) exécute dans l'ordre :

1. **Lecture JEDEC + correctif Flash Puya (`0x85`)** depuis une fenêtre SRAM avant le changement d'horloge (activation sécurisée du bit `QE`).
2. **Montée en fréquence à 252 MHz** avec timings QMI conservateurs (`63 MHz` pour la Flash, diviseur `4`).
3. **Initialisation PSRAM QSPI APS6404L (`CS1 = GPIO 47`)** en mode QPI à `126 MHz` (diviseur `2`), avec détection automatique de capacité (8 Mo) et test rapide de fiabilité.
4. **Démarrage du bus Oric et des deux cœurs ARM Cortex-M33**.

### Répartition Core 0 / Core 1 et périphériques PIO / DMA

| Cœur / Bloc | Rôle principal | Périphériques matériels |
| :--- | :--- | :--- |
| **Core 0** | CPU **6502** (`vrEmu6502`) cadencé à **1 MHz exact** par salves de `50 µs` (`busMainLoop` dans [`src/bus.c`](src/bus.c)), VIA 6522 (`$0300-$03FF`), ACIA 6551 (`$031C-$031F`), PSG AY-3-8910 (`emu2149`), lecteur cassette `.tap`, Dual USB Host, manette NES, Moniteur `F3` et Configurateur `F4`. | `USBCTRL_IRQ` (`rhport 0` natif), `PIO1` (`rhport 1` PIO-USB), `SPI1` (MicroSD), `I2C1` (`Wire1`), `UART0` (ACIA), `PWM_IRQ_WRAP_0` ou `DMA_IRQ_1` (audio). |
| **Core 1** | Génération du signal **DVI 640×480p @ 60 Hz** et rendu temps réel de l'**ULA Oric** ([`src/hdmi_dvi.c`](src/hdmi_dvi.c), [`src/ula.c`](src/ula.c)). Chaque scanline logique (`320×240`) est encodée en TMDS avec doublement horizontal et vertical pour centrer l'écran Oric (`240×224`) dans des bordures noires. | `PIO0` (`gpio_base = 16`, SM 0/1/2 TMDS D0/D1/D2), PWM Slice 11 (horloge TMDS), 6 canaux DMA chaînés (`DMA_IRQ_0`, priorité 0), 2 tampons ping-pong TMDS. |
| **`PIO2`** | Clavier **PS/2** (SM libre + file FIFO) et sortie audio **I2S** sur le firmware `i2s_audio` (SM libre). | Aucun conflit avec `PIO0` (DVI) ni `PIO1` (PIO-USB). |

### Fonctionnement ULA (`TEXT` et `HIRES`)

- Le rendu scanline par scanline est assuré par `ula_render_oric_line()` et `ula_scanline_tmds()` ([`src/ula.c`](src/ula.c)).
- L'attribut vidéo global (`pattr`, dont le bit `PATTR_HIRES = 0x04`) **persiste d'une trame à l'autre** à travers le retour trame vertical (`y = 223` → `y = 0`), conformément au fonctionnement matériel réel de l'ULA Oric :
  - En mode **TEXT** (`$BB80-$BFDF`), les motifs de caractères proviennent de `$B400` (standard) ou `$B800` (alternatif).
  - En mode **HIRES**, les lignes `0..199` lisent directement les octets graphiques de `$A000-$BF3F`, et les **3 lignes texte inférieures** (`y = 200..223`, `$BF68-$BFDF`) restent affichables avec curseur clignotant grâce à la table de caractères `$9800` (ou `$9C00` alternatif).
- **Important** : l'entrée dans le Moniteur (`F3`) ou le Configurateur (`F4`) sauvegarde `pattr` dans `pattrSave` et force temporairement le mode texte pour afficher l'OSD `40×28` depuis `ramMoni`, puis restaure fidèlement `pattr` en sortant.

### Bus mémoire, Microdisc, traps ROM et extensions

- **RAM Oric** : `48 Ko` (`$0000-$BFFF`), **ROM Oric Atmos v1.1** : `16 Ko` (`$C000-$FFFF`).
- **Deux modes machine** (onglet `REGLAGES` dans `F4`, persisté via `microdisc=0/1` dans `/oric.cfg`) :
  - **Atmos Simple (TAP)** : Atmos v1.1 pur pour cassettes `.TAP`.
  - **Atmos Microdisc (DSK)** : contrôleur **WD1793** (`$0310-$0313`), registres Microdisc `$0314` / `$0318`, **ROM Microdisc `8 Ko`** (`$E000-$FFFF`, [`src/microdisc_rom.c`](src/microdisc_rom.c)) et **16 Ko d'Overlay RAM** (`$C000-$FFFF`). Sans disquette, le boot affiche `Insert system disc` puis démarre dès qu'une image `.DSK` (`MFM_DISK`, simple ou double face) est montée via `F4`. Les disquettes (`~1,64 Mo` max par lecteur) résident en **PSRAM (`0x15200000`)** en lecture et écriture (secteurs + formatage piste), avec sauvegarde manuelle (`Sauver .DSK -> SD`) et auto-sauvegarde différée sur MicroSD.
- **I/O** : Microdisc sur `$0310-$0318` (si actif), ACIA 6551 sur `$031C-$031F` (adossé à `UART0`), VIA 6522 décodé sur le reste de `$0300-$03FF`.
- **Traps ROM cassette** (`$E4AC`, `$E735-$E737`, `$E6C9-$E6CB`, `$E92C-$E92E`) : actifs uniquement lorsque la ROM Atmos est paginée (`!microdisc_romdis()`). Lecture streaming `.tap` depuis MicroSD (cache 512 octets dans [`src/arduino_hal.cpp`](src/arduino_hal.cpp)), sauvegarde `CSAVE`. Les routines `$F5DC-$F5DD` sont patchées en `NOP`.
- **Extensions BASIC `!`** (trap `$CD13`, voir [`src/bus.c`](src/bus.c) et [`src/tape.c`](src/tape.c)) :
  - `!DIR` : liste les fichiers `.tap` présents à la racine de la carte SD.
  - `!LOAD "NOM"` : monte le fichier `NOM.tap` pour le prochain `CLOAD`.
  - `!EJECT` : éjecte la cassette en cours.
  - `!LED <0|1>` : pilote la LED d'activité (`GPIO 25`).
  - `!I2CW <addr>,<reg>,<val>` / `!I2CR <addr>,<reg>` : accès I2C direct (résultat lu dans `$0035`).

---

## 3. Affectation complète des GPIO (Waveshare RP2350-PiZero)

Références source : [`src/config.h`](src/config.h) et [`src/board_hw.h`](src/board_hw.h). Le connecteur GPIO 40 broches suit l'implantation mécanique Raspberry Pi.

| GPIO RP2350 | Broche Header 40 pins | Périphérique / Module | Fonction | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **GPIO 2** | **Pin 3** | I2C1 (`Wire1`) | `SDA` | Bus I2C utilisateur / `!I2CR`/`!I2CW` (400 kHz) |
| **GPIO 3** | **Pin 5** | I2C1 (`Wire1`) | `SCL` | Bus I2C utilisateur |
| **GPIO 5** | **Pin 29** | Manette NES (Port 1) | `NES_CLOCK` | Compatible schéma `pico-infonesPlus` |
| **GPIO 6** | **Pin 31** | Manette NES (Port 1) | `NES_DATA` | Entrée avec pull-up interne activé |
| **GPIO 9** | **Pin 21** | Manette NES (Port 1) | `NES_LATCH` | Latch `CD4021` (actif haut 12 µs) |
| **GPIO 14** | **Pin 8** | UART0 (ACIA 6551) | `UART0_TX` | Liaison série 6551 + sortie imprimante Oric |
| **GPIO 15** | **Pin 10** | UART0 (ACIA 6551) | `UART0_RX` | Réception série FIFO 512 octets |
| **GPIO 16** | **Pin 36** | Clavier PS/2 (`PIO2`) | `PS2_DATA` | Pull-up interne actif |
| **GPIO 17** | **Pin 11** | Clavier PS/2 (`PIO2`) | `PS2_CLOCK` | Doit être `PS2_DATA + 1` |
| **GPIO 18** | **Pin 12** | Audio I2S (`i2s_audio`) | `I2S_BCLK` (`BCK`) | Horloge bit `3,072 MHz` (`64·fS`), drive 8 mA |
| **GPIO 19** | **Pin 35** | Audio I2S (`i2s_audio`) | `I2S_LRCLK` (`LCK`) | Word Select `48,000 Hz`, doit être `I2S_BCLK + 1` |
| **GPIO 21** | **Pin 40** | Audio I2S (`i2s_audio`) | `I2S_DOUT` (`DIN`) | Données I2S MSB-first 32 bits/canal |
| **GPIO 25** | **Pin 22** | Diagnostic / BASIC | `ORIC_PIN_LED` | Clignotement boot + commande `!LED` |
| **GPIO 26** | **Pin 37** | Audio PWM (`pwm_audio`) | `AUDIO_PWM` | Slice 5 Ch A, porteuse **96 kHz** (filtrage RC requis) |
| **GPIO 28** | Interne (USB Type-C `PIO-USB`) | USB Host `rhport 1` (`PIO1`) | `USB_HOST_DP` | Port PIO-USB intégré sur PCB |
| **GPIO 29** | Interne (USB Type-C `PIO-USB`) | USB Host `rhport 1` (`PIO1`) | `USB_HOST_DM` | Doit être `USB_HOST_DP + 1` |
| **GPIO 30** | Interne (Lecteur TF/MicroSD) | MicroSD (`SPI1`) | `SD_SCK` | Bus SPI1 @ 20 MHz |
| **GPIO 31** | Interne (Lecteur TF/MicroSD) | MicroSD (`SPI1`) | `SD_MOSI` | Bus SPI1 |
| **GPIO 32 / 33** | Interne (Mini-HDMI) | DVI (`PIO0`, `gpio_base=16`) | `TMDS D2+/-` (Rouge) | Drive 4 mA, slew fast |
| **GPIO 34 / 35** | Interne (Mini-HDMI) | DVI (`PIO0`, `gpio_base=16`) | `TMDS D1+/-` (Vert) | Drive 4 mA, slew fast |
| **GPIO 36 / 37** | Interne (Mini-HDMI) | DVI (`PIO0`, `gpio_base=16`) | `TMDS D0+/-` (Bleu + Sync) | Drive 4 mA, slew fast |
| **GPIO 38 / 39** | Interne (Mini-HDMI) | DVI (PWM Slice 11) | `TMDS CLK+/-` (25,2 MHz) | Horloge pixel différentielle |
| **GPIO 40** | Interne (Lecteur TF/MicroSD) | MicroSD (`SPI1`) | `SD_MISO` | Bus SPI1 |
| **GPIO 43** | Interne (Lecteur TF/MicroSD) | MicroSD (`SPI1`) | `SD_CS` | Chip Select logiciel (`SD.begin`) |
| **GPIO 47** | Interne (PSRAM QSPI) | PSRAM `APS6404L` | `QMI CS1` | Mappée à `0x11000000` (126 MHz QPI) |

---

## 4. Schémas Audio externes

### A. Filtre passe-bas pour `pwm_audio` (`GPIO 26`, broche 37)

Le backend PWM ([`src/audio.c`](src/audio.c)) génère une porteuse à **96 000 Hz** (`252 MHz / 2625`, pas 11+ bits) modulée à **48 000 Hz** autour de `VDD/2` (`1,65 V`), avec une excursion d'environ **±1,5 V** (`±1200 / 2625`).

#### Option 1 — Cellule RC simple (1er ordre, rapide sur breadboard)
- **$R = 220\;\Omega$** (ou $330\;\Omega$) en série entre `GPIO 26` (pin 37) et le nœud de sortie filtrée.
- **$C = 47\;\text{nF}$** (céramique NP0/C0G ou film polyester) entre le nœud de sortie filtrée et `GND` (pin 39 ou 6).
- **Condensateur de liaison DC (obligatoire vers ampli / casque / entrée ligne)** : **$C_{\text{couplage}} = 10\;\mu\text{F}$** (ou $47\;\mu\text{F}$) en série entre la sortie filtrée et le signal audio (`+` côté GPIO si condensateur polarisé), avec une résistance de rappel optionnelle de $10\;\text{k}\Omega$ vers `GND` côté prise jack.
- Fréquence de coupure :
  $$f_c = \frac{1}{2\pi \cdot 220\,\Omega \cdot 47\,\text{nF}} \approx 15{,}4\;\text{kHz}$$
  (avec $330\;\Omega$ et $33\;\text{nF}$ : $f_c \approx 14{,}6\;\text{kHz}$ — excellente atténuation de la porteuse à 96 kHz d'environ $-16\;\text{dB}$ dès le 1er ordre, tout en préservant les aigus audio de l'Oric).

```text
GPIO 26 (Pin 37) ──[ 220 Ω ]──┬──[ 10 µF ]──► Sortie Audio (vers Ampli / Ligne)
                              │
                           [ 47 nF ]
                              │
GND (Pin 39) ─────────────────┴──────────────► Masse Audio (GND)
```

#### Option 2 — Double cellule RC (2e ordre, résiduelle porteuse quasi nulle)
Si vous branchez des écouteurs sensibles ou un ampli Hi-Fi, enchaînez deux cellules identiques (**$-32\;\text{dB}$ à 96 kHz**) :
- Étage 1 : **$R_1 = 220\;\Omega$**, **$C_1 = 47\;\text{nF}$**
- Étage 2 : **$R_2 = 470\;\Omega$**, **$C_2 = 22\;\text{nF}$**
- Condensateur série de sortie : **$10\;\mu\text{F}$**

### B. Câblage DAC I2S `PCM5102A` (`i2s_audio`)

Le driver I2S ([`src/audio.c`](src/audio.c)) envoie une trame **Philips I2S standard 32 bits/canal** (`64·fS` → `BCK = 3,072 MHz` exact, `LRCK = 48,000 Hz`, données 16 bits sur les bits de poids fort) :

| Broche PCM5102A | Connexion RP2350-PiZero | Rôle |
| :--- | :--- | :--- |
| `VIN` / `VCC` | `5V` (pin 2 ou 4) ou `3.3V` (selon régulateur du module) | Alimentation |
| `GND` | `GND` (pin 6, 9, 14, 20, 30, 34 ou 39) | Masse commune |
| `BCK` | **GPIO 18** (pin 12) | Bit Clock `3,072 MHz` |
| `LCK` / `LRCK` | **GPIO 19** (pin 35) | Left/Right Clock `48 kHz` |
| `DIN` | **GPIO 21** (pin 40) | Données I2S stéréo |
| `SCK` | **`GND`** (impératif : fil ou pont de soudure vers GND) | Active la PLL interne du PCM5102A sur `BCK` |
| `XSMT` | **`3.3V`** (ou pont `H3L` ouvert) | Désactive le mute matériel |
| `FMT` / `FLT` / `DEMP` | **`GND`** (état bas par défaut `H1L/H2L/H4L`) | Format I2S standard, filtre normal, sans de-emphasis |

---

## 5. Mode d'emploi : Clavier, Manette NES, Menu `F4` (`.TAP` / `.DSK`) et Moniteur (`F3`)

### A. Claviers et raccourcis globaux

- **Clavier Dual USB HID + PS/2** : fonctionne indifféremment sur le port **Type-C `USB`** natif (`rhport 0`), le port **Type-C `PIO-USB`** (`rhport 1`), ou le connecteur **PS/2**. Les concentrateurs USB (hubs) HID Boot et Report sont pris en charge ([`src/usb_kbd.cpp`](src/usb_kbd.cpp)), avec bascule **QWERTY / AZERTY FR** ([`src/usb_hid_map.c`](src/usb_hid_map.c), [`src/ps2_kbd.c`](src/ps2_kbd.c)).
- **Raccourcis clavier** :
  - `F1` : NMI 6502 (interruption non masquable)
  - `F2` : Reset complet de l'Oric (conserve le mode actif et la disquette `.DSK` montée)
  - `F3` : Ouvrir / quitter le **Mini-Moniteur 6502** ([`src/monitor.c`](src/monitor.c))
  - `F4` ou `F11` (ou **`Start + Select`** sur la manette NES) : Ouvrir / quitter le **Super-Configurateur OSD** ([`src/config_menu.c`](src/config_menu.c))

### B. Navigation dans le Configurateur OSD (`F4`)

Le configurateur s'affiche en mode texte `40×28` sans écraser la mémoire vidéo de l'Oric :

| Action | Au clavier (USB ou PS/2) | À la manette NES |
| :--- | :--- | :--- |
| **Ouvrir / Fermer le menu** | `F4`, `F11`, `F12` ou `ESC` | `Start + Select` (ouvrir), `B` (fermer/retour) |
| **Changer d'onglet (`1..4`)** | `TAB`, touches `1` à `4`, ou `Gauche` / `Droite` | Bouton `Select` (ou `Gauche` / `Droite`) |
| **Déplacer la sélection** | Flèches `Haut` / `Bas` | Croix `Haut` / `Bas` |
| **Valider / Monter un fichier** | `Entrée` ou `Espace` | Bouton `A` |
| **Modifier un réglage / Slot** | Flèches `Gauche` / `Droite` ou `Entrée` | Croix `Gauche` / `Droite` ou `A` |
| **Sortir de la liste `.TAP`/`.DSK`** | `ESC` | Bouton `B` |

### C. Petit mode d'emploi : Cassettes `.TAP` (Mode Atmos Simple)

Par défaut, l'émulateur démarre en **`Atmos Simple (TAP)`** sur le prompt `Ready` du BASIC v1.1 :

1. Copiez vos fichiers `.tap` à la **racine de la carte MicroSD** (formatée en FAT32 / exFAT).
2. Appuyez sur **`F4`** → l'onglet **`[1:FIC]` (`FICHIERS`)** s'ouvre.
3. Sélectionnez **`Choisir cassette .TAP sur SD...`** puis appuyez sur **`Entrée`** :
   - Parcourez la liste avec `Haut` / `Bas` et validez le fichier `.tap` avec **`Entrée`**.
   - La ligne d'état en haut de l'onglet affiche désormais `K7 : NOM_DU_JEU (<taille>o)`.
4. Pour lancer le chargement automatiquement :
   - **`Taper CLOAD"" dans l'Oric`** : ferme le menu et tape automatiquement `CLOAD""` + `Entrée` dans le BASIC.
   - **`Reset Oric + Lancer CLOAD""`** : réinitialise l'Oric à froid, remonte la cassette et tape `CLOAD""` dès que le BASIC est prêt (idéal pour changer de jeu à tout moment).
5. Vous pouvez aussi charger ou sauver depuis le BASIC Oric avec `!DIR`, `!LOAD "NOM"`, `CLOAD""` et `CSAVE "NOM"`.

### D. Petit mode d'emploi : Disquettes `.DSK` (Mode Atmos Microdisc)

L'émulateur prend en charge les disquettes Oric au format standard **`MFM_DISK` (`.dsk`)**, simple ou double face, en **lecture et écriture** :

#### Méthode 1 — Démarrer comme sur un vrai Oric Microdisc
1. Appuyez sur **`F4`**, puis passez à l'onglet **`[2:CFG]` (`REGLAGES`)** (touche `TAB` ou `2`).
2. Sur la première ligne **`Mode Machine`**, appuyez sur `Gauche` / `Droite` ou `Entrée` pour afficher **`<Atmos Microdisc>`** :
   - L'Oric effectue un reset immédiat avec le contrôleur Microdisc actif.
   - *(Optionnel)* Descendez sur **`Sauver config -> SD (/oric.cfg)`** et validez par `Entrée` si vous souhaitez que l'Oric démarre directement en mode Microdisc à chaque mise sous tension.
3. Appuyez sur **`ESC`** pour revenir à l'Oric : l'écran affiche **`Insert system disc`**.
4. Appuyez sur **`F4`** → onglet **`[1:FIC]` (`FICHIERS`)** → **`Inserer disquette .DSK (A:)...`** :
   - Choisissez votre fichier `.dsk` (ex. `Pulsoids.dsk`, `Sedoric3.dsk`…) et validez avec **`Entrée`**.
   - Appuyez sur **`ESC`** : le contrôleur détecte l'insertion et **démarre immédiatement la disquette** (SEDORIC, Oric-DOS, FT-DOS ou jeu auto-boot) sans même nécessiter de Reset.

#### Méthode 2 — Raccourci direct depuis le mode Atmos Simple
- Si vous êtes en mode `Atmos Simple (TAP)` et que vous choisissez directement un fichier via **`Inserer disquette .DSK (A:)...`**, l'émulateur **bascule automatiquement en mode `Atmos Microdisc` et boote la disquette** dès que vous quittez le menu (`ESC`).

#### Écriture, changement de disquette et sauvegarde sur carte SD
- **Indicateur `[MOD]`** : dès qu'un programme ou SEDORIC écrit un secteur ou formate une piste sur la disquette en PSRAM, la première ligne de l'onglet `1:FIC` affiche `DSK A: nom.dsk [MOD]`.
- **Auto-sauvegarde transparente** : après **~1,5 seconde** sans nouvelle activité disque (`COP_NUFFINK`), l'émulateur réécrit automatiquement l'image `.dsk` modifiée sur la carte MicroSD (`microdisc_autoflush()`).
- **Sauvegarde manuelle / Éjection** :
  - **`Sauver disquette .DSK -> SD`** : force immédiatement l'écriture du `.dsk` de la PSRAM vers la carte MicroSD.
  - **`Ejecter disquette .DSK (A:)`** : sauvegarde toute modification en attente puis retire la disquette virtuelle du lecteur `A:`.
  - **`Reset Oric (Booter Microdisc)`** (ou touche `F2`) : redémarre l'Oric à froid sur la disquette actuellement insérée en `A:`.
  - **Changement de disquette à chaud (multi-disquettes)** : sous SEDORIC ou dans un jeu multi-faces/multi-disquettes, ouvrez simplement `F4` → `Inserer disquette .DSK (A:)...`, sélectionnez la nouvelle disquette puis appuyez sur `ESC` (l'ancienne disquette modifiée est automatiquement sauvée avant le remplacement).

### E. Autres outils des 4 onglets `F4` et du Moniteur `F3`

- **Onglet `[1:FIC]` (`FICHIERS`)** :
  - **`Capture ecran -> CAPTURE.BMP`** : enregistre l'écran Oric courant (`240×224` 24 bits) à la racine de la carte SD.
  - **`PSRAM Save` / `PSRAM Load` (`Slots A, B, C, D`)** : 4 emplacements de sauvegarde d'état instantanée en PSRAM (`Gauche`/`Droite` pour choisir le slot `A..D`, `Entrée` pour sauver ou charger avec vérification de somme de contrôle FNV-1a).
  - **`Snapshot SD -> QUICK.SNA` / `Charger SD <- QUICK.SNA`** : sauvegarde et restauration d'état sur carte MicroSD.
- **Onglet `[2:CFG]` (`REGLAGES`)** :
  - **`Mode Machine`** : `<Atmos Simple (TAP)>` ou `<Atmos Microdisc>` (`microdisc=0/1`). *(Sécurité au démarrage : si aucune disquette `.DSK` n'est montée, le système démarre en mode Atmos Simple pour éviter un blocage sur l'écran Microdisc).*
  - **`Sortie Audio`** : `<HDMI 48kHz>`, `<I2S PCM5102>` ou `<PWM GPIO26>` (`audio_out=0/1/2`) — commutation immédiate à chaud du moteur audio et du protocole vidéo (HDMI 1.4b Data Islands en mode HDMI, DVI pur en mode I2S ou PWM).
  - **`Clavier`** : `<QWERTY US>` ou `<AZERTY FR>` (`azerty=0/1`).
  - **`Palette Video`** : `<RGB Couleur>`, `<Vert CRT>`, `<Ambre CRT>`, `<Noir&Blanc>` (`palette=0..3`).
  - **`Etat Audio` / `Volume Audio` / `Filtre DC PSG`** : activation (`ACTIF / MUET`), volume (`25%, 50%, 75%, 100%`) et filtre passe-haut DC du PSG AY-3-8910.
  - **`Manette NES`** : `<Perso (PAD)>`, `<Fleches+Esp>`, `<ZQSD+Esp>`, `<WASD+Esp>`, `<IJKL+Ret>`.
  - **`Sauver config -> SD (/oric.cfg)`** et **`Valeurs par defaut`**.
- **Onglet `[3:MAT]` (`MATERIEL`)** : fréquence RP2350B (`252 MHz`), ID JEDEC Flash + statut du correctif Puya QE, taille/ID/timings QMI et test de la PSRAM APS6404L (`8 Mo`), état carte SD, scan live du bus `I2C1` (touche `R` pour relancer) et visualisation en direct des boutons de la manette NES.
- **Onglet `[4:USB]` (`USB HOST`)** : état des ports `rhport 0` (natif) et `rhport 1` (`PIO-USB`), compteurs d'erreurs/réarmements et liste `VID:PID` des périphériques HID connectés.
- **Onglet `[5:PAD]` (`MANETTE NES`)** : **configuration directe sans fichier CSV** :
  - Associez individuellement chaque bouton NES (**Haut, Bas, Gauche, Droite, A, B, Select, Start**) à n'importe quelle touche du clavier Oric (`ESPACE`, `RETURN`, `CTRL`, `L-SHIFT`, `R-SHIFT`, `ESC`, `FCT`, `DEL`, flèches, lettres `A..Z`, chiffres `0..9`, ponctuation, ou `AUCUN`).
  - Deux méthodes : **Gauche / Droite** pour parcourir la liste des touches, ou **Entrée** puis **appui direct sur la touche souhaitée** sur votre clavier USB ou PS/2.
  - Permet également d'appliquer un preset rapide (`Fleches+Esp`, `ZQSD+Esp`, `WASD+Esp`, `IJKL+Ret`) et de sauvegarder dans `/oric.cfg` (`nes_map=...`).
- **Mini-Moniteur (`F3`)** : commandes `H/?` (aide), `M <addr>` (dump hex/ASCII), `D <addr>` (désassembleur 6502), `R` (registres 6502 + VIA + ACIA + état cassette ou FDC `FDC:D0 Txx MD:xx WD:xx`), `S` (pas-à-pas), `G [addr]` (continuer), `MM` / `F` (écriture RAM), `FW` / `FR` (blocs `.bin`), `SNS` / `SNL` (snapshots `.sna`), `BMP`, `I2C`, `DIR`, `TAPE`, `RESET`, `Q/ESC` (quitter).

---

### Caractéristiques exactes du signal HDMI / DVI produit

Le moteur vidéo ([`src/hdmi_dvi.c`](src/hdmi_dvi.c) + [`src/hdmi_packet.c`](src/hdmi_packet.c)) génère un signal numérique standard **VESA / CEA-861 640×480p @ 60 Hz** verrouillé sur l'horloge système de `252,000 MHz` :

| Paramètre | Valeur exacte | Détails |
| :--- | :--- | :--- |
| **Standard vidéo** | **640 × 480p @ 60,000 Hz** (CEA VIC 1) | Résolution logique `320 × 240` doublée ×2 en H et V (Oric `240 × 224` centré) |
| **Pixel Clock (TMDS CLK)** | **25,200 MHz** | `clk_sys` `252,000 MHz / 10` (PWM Slice 11 sur `GPIO 38/39`) |
| **Débit bit TMDS** | **252,0 Mbit/s par voie** | 3 paires DATA différentielles (`D0=36/37`, `D1=34/35`, `D2=32/33`), encodage 8b/10b |
| **Balayage horizontal** | **800 pixels / ligne (`31,500 kHz`)** | `640` actifs + `16` Front Porch + `96` Sync Pulse (négatif) + `48` Back Porch |
| **Balayage vertical** | **525 lignes / trame (`60,000 Hz`)** | `480` actives + `10` Front Porch + `2` Sync Pulse (négatif) + `33` Back Porch |
| **Format couleur** | **RGB 24 bits (8:8:8)** | Encodage TMDS direct par table précalculée (palettes RGB, Vert CRT, Ambre, Mono) |
| **Protocole** | **HDMI 1.4b** *(mode HDMI)* / **DVI-D pur** *(I2S/PWM)* | Data Islands TERC4/BCH4 dans les intervalles HBlank/VBlank en mode HDMI |
| **Audio HDMI (L-PCM)** | **48,000 kHz stéréo (2 ch)** | `N = 6144`, `CTS = 25200`, ratio exact `32/84` paquet/ligne (`200` paquets = `800` éch./trame) |
| **InfoFrames HDMI** | **AVI** (VIC 1 RGB), **VSIF** (`0x000C03`), **Audio** (L-PCM 2ch), **ACR** | Émis sur les lignes VBlank 1..4 |

---

## 6. Implémentation technique de l'émulation Microdisc

Fichiers sources : [`src/microdisc.h`](src/microdisc.h), [`src/microdisc.c`](src/microdisc.c), [`src/microdisc_rom.c`](src/microdisc_rom.c) et [`src/bus.c`](src/bus.c). L'implémentation est adaptée d'**Oricutron** (`disk.c` / `machine.c`) et optimisée pour l'architecture mémoire du RP2350-PiZero.

### A. Cartographie mémoire `$C000-$FFFF` (ROM Atmos, EPROM Microdisc et Overlay RAM)

En mode **`Atmos Microdisc`**, les `16 Ko` supérieurs de l'espace d'adressage 6502 (`$C000-$FFFF`) sont commutés dynamiquement par deux bits du registre d'écriture Microdisc **`$0314`** (`s_md.status`) :
- **Bit 1 (`MDSF_ROMDIS = 0x02`)** : signal `/ROMDIS` (actif à `0`).
  - `0` (`microdisc_romdis() == true`, état au reset) : la ROM BASIC Atmos `16 Ko` est **désactivée** au profit de l'Overlay RAM (`overlayRam[16384]` en SRAM interne) et/ou de l'EPROM Microdisc.
  - `1` (`microdisc_romdis() == false`) : la **ROM BASIC Atmos `16 Ko`** (`oricRom[]`) est visible en lecture sur `$C000-$FFFF` (écritures ignorées).
- **Bit 7 (`MDSF_EPROM = 0x80`)** : sélection `/EPROM` (actif à `0`).
  - `0` (`microdisc_diskrom() == true`, état au reset) : lorsque `romdis` est actif, les `8 Ko` de **`MICRODIS.ROM`** (`microdiscRom[8192]`) sont mappés en lecture seule sur **`$E000-$FFFF`**, tandis que **`$C000-$DFFF`** pointe sur les `8 Ko` bas de l'**Overlay RAM** en lecture/écriture.
  - `1` (`microdisc_diskrom() == false`) : lorsque `romdis` est actif, la totalité des **`16 Ko` (`$C000-$FFFF`)** est mappée sur l'**Overlay RAM** en lecture/écriture (configuration utilisée par SEDORIC une fois chargé).

| Bit 1 (`ROMDIS`) | Bit 7 (`EPROM`) | Plage `$C000-$DFFF` (8 Ko) | Plage `$E000-$FFFF` (8 Ko) | Traps ROM Cassette / `!` |
| :---: | :---: | :--- | :--- | :---: |
| **`0`** (Reset) | **`0`** (Reset) | **Overlay RAM** (R/W) | **`MICRODIS.ROM`** (Lecture seule) | Désactivés |
| **`0`** | **`1`** | **Overlay RAM** (R/W) | **Overlay RAM** (R/W) | Désactivés |
| **`1`** | `0` ou `1` | **ROM Atmos v1.1** (RO) | **ROM Atmos v1.1** (RO) | **Actifs** |

> **Isolation des traps ROM** : dans [`busRead()`](src/bus.c), les interceptions d'adresses de la ROM Atmos (`$E4AC`, `$E735`, `$E6C9`, `$E92C`, `$F5DC`, `$CD13`) sont exécutées uniquement dans la branche `!microdisc_romdis()`. Elles ne perturbent donc jamais le code de `MICRODIS.ROM` ni le système d'exploitation logé dans l'Overlay RAM.

### B. Motif DRAM 4164 à l'allumage et checksum warm-boot de SEDORIC 3.0

Au démarrage, le chargeur de **SEDORIC V3.0** (`$B92D-$B94D`) calcule une somme de contrôle 8 bits sur les `55` pages et demie de l'Overlay RAM (`$C980-$FFFF`) :
- Si la somme vaut **`0x00`**, SEDORIC considère que le système est déjà résident en Overlay RAM (reset à chaud) et ne charge que **4 secteurs** au lieu de **60 (`$3C`)**, avant de sauter en `$DA5D`.
- Sur une RAM initialisée intégralement à `0x00`, la somme de `0 + 0 + ...` vaut `0x00`, ce qui provoque un crash immédiat (`BRK`) après l'affichage de la bannière `SEDORIC V3.0`.
- Pour reproduire le comportement matériel des puces DRAM **4164** de l'Oric (et la fonction `blank_ram()` d'Oricutron), `init_overlay_ram_pattern()` dans [`src/bus.c`](src/bus.c) initialise chaque page de 256 octets de `overlayRam[]` avec **`128` octets à `0x00` suivis de `128` octets à `0xFF`**. La demi-page `$C980-$C9FF` donne ainsi une somme initiale de `0x80 != 0x00`, garantissant le chargement complet des 60 secteurs de SEDORIC.

### C. Décodage des registres d'entrées/sorties (`$0300-$03FF`)

Lorsque le mode Microdisc est activé, la page `$0300-$03FF` est décodée comme suit dans [`busRead()`](src/bus.c) et [`busWrite()`](src/bus.c) :

| Adresse | Lecture (`busRead`) | Écriture (`busWrite`) | Détails des bits |
| :--- | :--- | :--- | :--- |
| **`$0310`** | **WD1793 Status** (`r_status`) — acquitte `/INTRQ` | **WD1793 Command** | Commandes Type I (`Restore $00`, `Seek $10`, `Step $20/$40/$60`), Type II (`Read Sector $80/$90`, `Write Sector $A0/$B0`), Type III (`Read Addr $C0`, `Read/Write Track $E0/$F0`), Type IV (`Force IRQ $D0`). |
| **`$0311`** | **WD1793 Track** (`r_track`) | **WD1793 Track** (`r_track`) | Numéro de piste courante (`0..127`). |
| **`$0312`** | **WD1793 Sector** (`r_sector`) | **WD1793 Sector** (`r_sector`) | Numéro de secteur cible (`1..32`). |
| **`$0313`** | **WD1793 Data** (`r_data`) — acquitte `DRQ` | **WD1793 Data** (`r_data`) — acquitte `DRQ` | Octet transféré depuis/vers le secteur ou la piste MFM + mise à jour du CRC16 CCITT (`calc_crc()`). |
| **`$0314`** | **`INTRQ \| 0x7F`** | **Registre de contrôle Microdisc** (`s_md.status`) | En lecture : **bit 7 = `/INTRQ`** (`0` = interruption WD1793 active, `1` = inactive), bits `6..0 = 1`. En écriture : bit 0 = `INTENA`, bit 1 = `ROMDIS`, bit 4 = `SIDE` (face `0/1`), bits `6..5` = `DRIVE` (`0..3`), bit 7 = `EPROM`. |
| **`$0318`** | **`DRQ \| 0x7F`** | Latch `DRQ` | En lecture : **bit 7 = `/DRQ`** (`0` = octet prêt à lire/écrire dans `$0313`, `1` = attente), bits `6..0 = 1`. |
| **`$0315-$0317`**, **`$0319-$031B`** | **VIA 6522** (`addr & 0x0F`) | **VIA 6522** (`addr & 0x0F`) | Retombée sur le VIA 6522 (conforme au décodage partiel d'Oricutron). |
| **`$031C-$031F`** | **ACIA 6551** (`addr & 0x03`) | **ACIA 6551** (`addr & 0x03`) | Liaison série `UART0`. |
| **Autres `$03xx`** | **VIA 6522** (`addr & 0x0F`) | **VIA 6522** (`addr & 0x0F`) | Timers, clavier matriciel, PSG AY-3-8910. |

> **Interruptions CPU** : la ligne `/IRQ` du 6502 (`update_cpu_irqs()`) est activée par le OU logique entre le VIA 6522 (`g_via.ifr & 0x80`), l'ACIA 6551 (`acia_irq_pending()`) et le Microdisc (`microdisc_irq_pending()`, vraie lorsque `INTRQ == 0` et `MDSF_INTENA == 1`).

### D. Correctif `MICRODISC_FUDGE` (`$E3A1`) pour `"Insert system disc"`

La ROM originale `MICRODIS.ROM` comporte un bug connu dans sa routine de détection de lecteur : si aucune disquette n'est présente lors de la commande `Restore` initiale exécutée par l'instruction `STY $0310` à l'adresse **`$E3A1`**, la pile 6502 se déséquilibre.
Comme Euphoric et Oricutron :
1. Lorsque `busWrite()` intercepte une écriture en `$0310` avec `vrEmu6502GetCurrentOpcodeAddr(g_cpu) == 0xE3A1` et qu'aucune disquette n'est montée, `wd_seek_track()` renvoie le statut `0x44` (`WSFI_WRPROT | WSFI_TRK0`).
2. La commande `Read Sector` suivante passe dans l'état d'attente **`COP_READ_SECTOR_FUDGE`** (ou `COP_READ_SECTORS_FUDGE`), ce qui permet à `MICRODIS.ROM` d'afficher proprement **`Insert system disc`**.
3. Dès que l'utilisateur monte un fichier `.dsk` via le menu `F4`, `microdisc_tick()` détecte `active_disk() != NULL` et convertit automatiquement l'opération en `COP_READ_SECTOR`, déclenchant le boot immédiat de la disquette.

### E. Organisation de la PSRAM (`8 Mo`) et persistance des `.DSK`

Le format Oric **`MFM_DISK`** stocke chaque piste sous forme d'un flux MFM brut de **`6 400` octets** (marqueurs de synchronisation `0xA1 0xA1 0xA1 0xFE` pour l'en-tête ID `[piste, face, secteur, taille N, CRC16]` et `0xA1 0xA1 0xA1 0xFB`/`0xF8` pour les données + `CRC16`).
Afin de supporter jusqu'à `2` faces de `128` pistes (`256 + 2 × 128 × 6 400 = 1 638 656` octets par lecteur) sans consommer la SRAM interne du RP2350 :

- **Cartographie de la PSRAM APS6404L (`8 Mo`)** :
  - Accès des snapshots via la fenêtre cachée `0x11000000` (`BOARD_PSRAM_BASE`) et accès des disquettes via l'alias **XIP non-caché `0x15000000`** (`MICRODISC_PSRAM_UNCACHED`, évitant toute éviction du cache XIP de 16 Ko utilisé par le code Flash) :
    - `0x100000 .. 0x13FFFF` (`256 Ko`) : **4 slots de snapshots rapides** (`Slots A..D`, `64 Ko` chacun).
    - `0x200000 .. 0x3900FF` (`~1,64 Mo`) : **Image brute `.DSK` du lecteur `A:` (`Drive 0`)**.
    - `0x400000 .. 0x5900FF` (`~1,64 Mo`) : **Image brute `.DSK` du lecteur `B:` (`Drive 1`)**.
- **Normalisation double face et indexation des pistes** :
  - Au chargement (`finalize_disk_mount()`), si le fichier `.dsk` possède 2 faces avec `numtracks_in < 128` (ex. 42 ou 82 pistes), la face 1 est déplacée à l'offset fixe `256 + 128 × 6 400`, exactement comme dans Oricutron.
  - Lors de chaque changement de piste (`diskimage_cachetrack()`), la piste courante (`6 400` octets) est parcourue une seule fois pour mettre en cache les pointeurs `id_ptr` et `data_ptr` des secteurs (jusqu'à 32 secteurs/piste). Un formatage de piste (`COP_WRITE_TRACK`, commande `$F0/$F4`) invalide automatiquement ce cache en fin de piste.
- **Transferts MicroSD sécurisés par tampon SRAM (`s_ioBounce[4096]`)** :
  - Les lectures (`microdisc_insert_disk()`) et sauvegardes (`microdisc_save_disk()`) sur carte MicroSD passent par un tampon de rebond de `4 Ko` aligné en SRAM interne afin que la pile SPI/DMA `SdFat` n'accède jamais directement au bus QMI PSRAM.

---

## 7. Compilation (PlatformIO)

```bash
# Compilation du firmware unifie (HDMI + I2S + PWM)
pio run -d versionhdmi -e oric_rp2350_pizero
```

Le fichier `.uf2` produit dans `.pio/build/oric_rp2350_pizero/firmware.uf2` est copié à la racine sous `oric_rp2350_pizero.uf2`.
