# Émulateur Commodore 64 (Frodo4) pour Waveshare RP2350-PiZero

Émulateur complet du **Commodore 64** (basé sur le cœur Frodo4 par ligne), adapté pour la carte **Waveshare RP2350-PiZero** (RP2350B @ 252 MHz, 16 Mo Flash QSPI, 8 Mo PSRAM APS6404L), avec sortie vidéo **HDMI/DVI 640×480p60** et **audio unifié 3-en-1 (HDMI / I2S / PWM)**.

---

## 1. Compatibilité des fichiers (`.PRG`, `.D64`, `.CRT`)

- **Fichiers `.PRG` (Recommandé — Fonctionne parfaitement)** :
  - Les programmes et jeux au format `.PRG` se chargent directement en RAM et démarrent automatiquement via **F11**.
  - Vous pouvez trouver des milliers de jeux, démos et utilitaires `.PRG` sur **[CSDb (The Commodore 64 Scene Database)](https://csdb.dk/)**.
https://www.zimmers.net/anonftp/pub/cbm/c64/index.html
https://archive.org/download/arnold.c64.org

- **Fichiers `.D64` et `.CRT` (Limites du cœur Frodo par ligne)** :
  - Les disquettes `.D64` contenant des jeux à *fastloader* 2-bit synchrone au cycle près (comme beaucoup de cracks commerciaux) et les cartouches `.CRT` utilisant des décompresseurs ou mappers complexes ne sont pas supportés par le cœur Frodo par ligne (nécessaire pour tenir les 50 Hz à 252 MHz sur RP2350).
  - Les disquettes `.D64` standard (sans *fastloader*) restent utilisables en mode `Fast IEC (DOS)`.
- **Sauvegarde de vos propres programmes BASIC (`.PRG`)** :
  - L'émulateur intègre le pilote `FSDrive` sur le lecteur 8 connecté au dossier `/c64` de la carte MicroSD : vous pouvez écrire vos programmes en BASIC et les sauvegarder directement en `.PRG` avec `SAVE"MONPROG",8` !

---

## 2. Branchements détaillés des GPIO (Waveshare RP2350-PiZero)

### A. Connectique intégrée sur la carte (aucun câblage requis)
| Périphérique | Broches RP2350B | Détails |
| :--- | :--- | :--- |
| **Mini-HDMI / DVI** | GPIO 32/33 (D2 Rouge)<br>GPIO 34/35 (D1 Vert)<br>GPIO 36/37 (D0 Bleu)<br>GPIO 38/39 (CLK) | PIO0 (base GPIO 16), 640×480p60 + audio L-PCM 48 kHz |
| **USB Type-C (Port 0)** | USB natif RP2350 | Clavier USB, hub USB, manette USB HID, adaptateur 2.4 GHz |
| **USB Type-C (Port 1)** | GPIO 28 (D+), GPIO 29 (D-) | PIO-USB Host (PIO1) pour 2ᵉ clavier ou manette USB |
| **Lecteur MicroSD (TF)** | GPIO 30 (SCK), GPIO 31 (MOSI)<br>GPIO 40 (MISO), GPIO 43 (CS) | SPI1 matériel (carte formatée en FAT32) |
| **PSRAM 8 Mo** | GPIO 47 (CS1) | APS6404L soudée sur PCB (mappée XIP `0x11000000`) |

### B. Branchements sur le header 40 broches

#### 1. Manettes (NES + USB / Clavier)
Un port manette **NES** est câblé sur le header GPIO, et une **deuxième manette** peut être connectée simultanément sur un des ports USB Host (ou émulée au clavier). La touche **F9** permet d'inverser à la volée le **Port 1** et le **Port 2** du C64 (la majorité des jeux C64 utilisent le Port 2).

| Signal Manette NES | GPIO RP2350 | Broche Header 40 pts | Note |
| :--- | :--- | :--- | :--- |
| **3.3V (VCC)** | 3V3 | Broche 1 ou 17 | **Ne pas alimenter en 5V** (niveaux 3.3V sur RP2350) |
| **GND** | GND | Broche 6, 14, 20, 30, 34 ou 39 | Masse commune |
| **NES CLOCK** | GPIO 5 | **Broche 29** | Horloge registre CD4021 |
| **NES DATA** | GPIO 6 | **Broche 31** | Données série (pull-up interne activé) |
| **NES LATCH** | GPIO 9 | **Broche 21** | Verrouillage état boutons |

> **Raccourci manette NES** : `Select + Start` ouvre le menu de configuration OSD.

#### 2. Clavier PS/2
| Signal PS/2 | GPIO RP2350 | Broche Header 40 pts | Note |
| :--- | :--- | :--- | :--- |
| **PS/2 DATA** | GPIO 16 | **Broche 36** | Pull-up **4.7 kΩ** vers 3.3V recommandé |
| **PS/2 CLOCK** | GPIO 17 | **Broche 11** | Pull-up **4.7 kΩ** vers 3.3V recommandé |
| **VCC / GND** | 5V (ou 3.3V) / GND | Broche 2 ou 4 (5V) / Broche 6 (GND) | Selon tolérance du clavier PS/2 (utiliser un pont diviseur ou level-shifter si le clavier tire à 5V) |

#### 3. Liaison Série UART0 (Console / Debug)
| Signal UART0 | GPIO RP2350 | Broche Header 40 pts | Paramètres |
| :--- | :--- | :--- | :--- |
| **UART0 TX** | GPIO 14 | **Broche 8** | Vers RX de votre adaptateur USB-TTL (3.3V) |
| **UART0 RX** | GPIO 15 | **Broche 10** | Vers TX de votre adaptateur USB-TTL (3.3V) |
| **GND** | GND | **Broche 6** | 115200 bauds, 8N1 |

#### 4. Audio I2S Externe (DAC PCM5102A)
À sélectionner dans le menu **F4 -> Audio Output -> I2S PCM5102** :

| Signal DAC PCM5102 | GPIO RP2350 | Broche Header 40 pts | Note |
| :--- | :--- | :--- | :--- |
| **BCK (Bit Clock)** | GPIO 18 | **Broche 12** | Horloge bit I2S |
| **LCK / LRCK** | GPIO 19 | **Broche 35** | Horloge Word Select (48 kHz) |
| **DIN (Data In)** | GPIO 21 | **Broche 40** | Données audio PIO2 SM1 |
| **VCC / GND** | 3.3V ou 5V / GND | Broche 1 ou 2 / Broche 39 | Relier SCK du module PCM5102 à GND |

#### 5. Audio Analogique PWM
À sélectionner dans le menu **F4 -> Audio Output -> PWM Audio** :
- **Sortie PWM (96 kHz)** : **GPIO 26** (**Broche 37**). Utiliser un filtre passe-bas RC (ex : 1 kΩ en série + 10 nF à la masse) avant l'entrée ligne d'un ampli ou casque.

---

## 3. Raccourcis Clavier & Utilisation

| Touche | Action |
| :--- | :--- |
| **F4** ou **F12** | Ouvre le **Menu de Configuration OSD** (Audio HDMI/I2S/PWM, Volume, Palette, Mode 1541, Reset, RESTORE). |
| **Select + Start** (NES) | Ouvre également le Menu de Configuration OSD. |
| **F11** | Ouvre le **Sélecteur de fichiers MicroSD** (`/c64`). |
| **F9** | Inverse le joystick actif entre **Port 2** (défaut) et **Port 1**. |
| **Échap (Esc)** | Touche **RUN/STOP** sur le C64, ou fermeture des menus OSD. |
| **Flèches directionnelles** | Touches curseurs C64 (Gauche/Haut gèrent automatiquement `Shift`). |
| **Home** | Touche C64 **CLR/HOME**. |
| **Alt (Gauche/Droit)** | Touche C64 **Commodore (`C=`)**. |
| **Ctrl (Gauche/Droit)** | Touche C64 **CTRL**. |
| **Backspace** | Touche C64 **INST/DEL**. |
| **Entrée** | Validation dans les menus ou touche C64 **RETURN**. |

---

## 4. Commandes BASIC utiles (Charger, Lire, Exécuter, Sauvegarder)

Préparez une carte MicroSD en **FAT32** avec un dossier `/c64` à la racine contenant vos fichiers `.PRG` (téléchargeables notamment sur [https://csdb.dk/](https://csdb.dk/)).

### Méthode rapide (Menu F11)
1. Appuyez sur **F11**.
2. Sélectionnez votre fichier `.PRG` avec les flèches **Haut / Bas** (ou **Page Up / Page Down**).
3. Appuyez sur **Entrée** puis validez **Load & Run** : le fichier est chargé en mémoire et exécuté automatiquement.

### Commandes BASIC manuelles (Lecteur 8 = dossier `/c64` sur MicroSD)
- **Lister les fichiers `.PRG` présents dans `/c64`** :
  ```basic
  LOAD"$",8
  LIST
  ```
- **Charger un programme BASIC** (ex. `MONPROG.PRG`) :
  ```basic
  LOAD"MONPROG",8
  ```
- **Charger un programme en langage machine à son adresse d'origine** :
  ```basic
  LOAD"JEU",8,1
  ```
- **Afficher le code BASIC en mémoire** :
  ```basic
  LIST
  ```
- **Exécuter le programme chargé** :
  ```basic
  RUN
  ```
- **Effacer le programme en mémoire avant d'en écrire un nouveau** :
  ```basic
  NEW
  ```
- **Écrire et sauvegarder un programme BASIC sur la carte MicroSD** :
  ```basic
  10 PRINT "BONJOUR RP2350 !"
  20 GOTO 10
  SAVE"BONJOUR",8
  ```
  Le fichier `/c64/BONJOUR.PRG` est créé directement sur la carte MicroSD (et devient visible dans le menu **F11**).
- **Écraser / mettre à jour un fichier existant** :
  ```basic
  SAVE"@0:BONJOUR",8
  ```

> *Note : Si une image `.D64` est montée via F11 -> `Mount Only`, les commandes `LOAD`/`SAVE` s'appliquent à cette disquette. Pour revenir directement au dossier `/c64` de la carte MicroSD, faites un Reset via F4 ou montez un `.PRG`.*

---

## 5. Compilation avec PlatformIO

```bash
cd c64new
pio run
```

Le fichier UF2 généré est copié sous [c64_rp2350_pizero.uf2](file:///home/alecren/Documents/oric/c64new/c64_rp2350_pizero.uf2).
Pour flasher : maintenez **BOOT** en branchant la carte en USB, puis glissez-déposez `c64_rp2350_pizero.uf2` sur le lecteur `RP2350`.
