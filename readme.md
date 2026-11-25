# Émulateurs 8 bits sur Waveshare RP2350

Ce dépôt regroupe mes développements ainsi qu'une sélection de projets compatibles permettant d'émuler des ordinateurs et consoles 8 bits sur la carte **Waveshare RP2350-PiZero** (microcontrôleur Raspberry Pi RP2350B). **Cette carte a été choisie avant tout pour son port mini-HDMI intégré permettant de sortir directement la vidéo (via PIO)**, offrant ainsi une solution compacte pour le rétrogaming.

---

## 📂 Structure du dépôt

| Dossier | Description |
| :--- | :--- |
| **[`RP2350_Brief`](./RP2350_Brief)** | Documentation, schémas de câblage et présentation technique de la carte Waveshare RP2350. |
| **[`oric-atmos`](./oric-atmos)** | Fichiers sources, firmwares et ressources pour l'émulation de l'**Oric Atmos**. |
| **[`commodore64`](./commodore64)** | Fichiers sources, firmwares et ressources pour l'émulation du **Commodore 64**. |
| **[`apple2e`](./apple2e)** | Fichiers sources, firmwares et ressources pour l'émulation de l'**Apple IIe**. |

---

## 🖥️ Projets d'émulation (Dépôt local)

### 1. Oric Atmos
Émulation de l'Oric Atmos (processeur 6502) avec gestion de l'affichage et des périphériques.
* Accéder au dossier : [`/oric-atmos`](./oric-atmos)

![Émulateur Oric Atmos sur Waveshare RP2350](images/oric.png)

---

### 2. Commodore 64
Émulation du célèbre micro-ordinateur 8 bits de Commodore sur plateforme RP2350.
* Accéder au dossier : [`/commodore64`](./commodore64)
* Code adapté à partir de : [rh1tech/frank-c64](https://github.com/rh1tech/frank-c64)

![Émulateur Commodore 64 sur Waveshare RP2350](images/c64.png)

---

### 3. Apple IIe
Émulation du micro-ordinateur 8 bits Apple IIe (processeur 65C02) avec gestion des modes vidéo et images disques (`.dsk`, `.nib`, `.woz`).
* Accéder au dossier : [`/apple2e`](./apple2e)
* Code adapté à partir de : [rh1tech/frank-apple](https://github.com/rh1tech/frank-apple)

![Émulateur Apple IIe sur Waveshare RP2350](images/apple.png)

---

## 🌐 Projets externes compatibles avec cette carte

Ce dépôt recense également d'autres émulateurs majeurs compatibles avec la carte **Waveshare RP2350-PiZero** :

### 4. ZX Spectrum (`pico-speccy`)
Émulateur complet des ordinateurs compatibles Sinclair ZX Spectrum (48K, 128K, +3, Pentagon, Scorpion, ZX Spectrum Next, etc.) avec support HDMI, audio I2S/PCM5122 et double port USB (PIO-USB).
* Dépôt officiel : [drewpo28/pico-speccy](https://github.com/drewpo28/pico-speccy/blob/main/README.md)

![Émulateur ZX Spectrum pico-speccy](images/zx.jpg)

---

### 5. Nintendo NES (`pico-infonesPlus`)
Émulateur Nintendo Entertainment System (NES / Famicom Disk System) avec sortie HDMI, chargement depuis carte SD, menu OSD et support des manettes USB.
* Dépôt officiel : [PicoPlus-devel/pico-infonesPlus](https://github.com/PicoPlus-devel/pico-infonesPlus)

![Émulateur NES pico-infonesPlus](images/nes.png)

---

## 🛠️ Matériel requis & Modification PSRAM

* Carte de développement **Waveshare RP2350-PiZero** (voir détails dans [`RP2350_Brief`](./RP2350_Brief))
  * *Note vidéo & audio :* Sur cette carte, le connecteur mini-HDMI n'est pas câblé sur les broches HSTX mais sur des GPIO standards pilotés en **PIO** (compatibilité broche à broche avec la RP2040-PiZero). Selon les firmwares, l'audio est restitué via HDMI (selon le pilote utilisé) ou sur le connecteur 40 pins (PWM ou DAC I2S externe type **PCM5122**).
* **Ajout mémoire PSRAM (fortement recommandé) :**
  * La carte dispose d'un emplacement libre au dos (boîtier **SOP-8**) permettant de souder une puce **PSRAM APS6404L-3S** (capacité de **64 Mbit, soit 8 Mo**).
  * Cette extension de **8 Mo** est indispensable pour faire tourner les projets les plus gourmands (*frank-c64*, *frank-apple* en mode complet, *pico-speccy* pour le ZX Spectrum Next / TS-Conf) et permet sous *pico-infonesPlus* de charger instantanément les ROMs en RAM sans avoir à les flasher.
  * *Note : Sur certaines cartes équipées d'une mémoire Flash non-Winbond (ex. Puya), un correctif logiciel unique (`FLASH_QE_SET_1.uf2`) peut être nécessaire pour activer le bit Quad Enable (QE) avant d'utiliser la PSRAM en overclocking.*
* Clavier ou manette USB (connecté sur le port **PIO-USB** de la carte)
* Carte microSD (formatée en FAT32 ou exFAT) pour le stockage des ROMs et images disques (`.d64`, `.tap`, `.tzx`, `.dsk`, `.woz`, `.nes`, etc.)

---

## 🚀 Installation rapide (Flashage `.uf2`)
1. Téléchargez le fichier firmware `.uf2` correspondant à la carte **Waveshare RP2350-PiZero** depuis le dossier concerné ou la page *Releases* du projet.
2. Maintenez le bouton **BOOT** de la carte enfoncé tout en la raccordant à votre ordinateur via le port **USB** (et non PIO-USB).
3. Relâchez le bouton une fois que le lecteur amovible `RP2350` apparaît.
4. Glissez-déposez le fichier `.uf2` sur ce lecteur ; la carte redémarre automatiquement sur l'émulateur.

---


