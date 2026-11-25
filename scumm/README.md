# Émulateur SCUMM v3/v4 pour Waveshare RP2350-PiZero

Portage du moteur **SCUMM v3/v4** (*Loom*, *Monkey Island 1 EGA*, *Indiana Jones 3 EGA*, démos ScummVM...) issu d'`Adafruit_Arcade_Machines` sur la carte **Waveshare RP2350-PiZero** (`RP2350B`, 16 Mo Flash, 8 Mo PSRAM).

---

## 1. Caractéristiques principales

- **Vidéo HDMI / DVI native (`PIO0`, `640×480 @ 60 Hz`)** : rendu `320×240` RGB565 (zone SCUMM `320×200` centrée) encodé en TMDS 0-disparité sur Core 1.
- **Audio unifié 3-en-1 (`48 kHz`)** *(commutable à chaud dans le menu `F4`)* :
  - **HDMI Audio** (Data Islands L-PCM `48 kHz`)
  - **I2S DAC** (`PCM5102` sur `GPIO 18/19/21`)
  - **PWM Audio** (`GPIO 26`, `96 kHz`)
  - Rééchantillonnage linéaire `22 050 Hz → 48 000 Hz` avec synthèse AdLib/PC Speaker.
- **Mémoire PSRAM `8 Mo` (`GPIO 47`)** : héberge l'arène de `2 Mo` du moteur SCUMM.
- **Carte MicroSD (`SPI1`, FAT32)** : lecture des ressources SCUMM et sauvegardes.
- **Entrées complètes (Hub USB supporté)** :
  - **Souris USB** (via port USB natif, PIO-USB ou **Hub USB**).
  - **Clavier USB** (via port USB natif, PIO-USB ou **Hub USB**).
  - **Clavier PS/2** direct (`PIO2`, `GPIO 16/17`).
  - **Manette NES** (`GPIO 5/6/9`) et **Manette USB HID** (émulent la souris et les clics).

---

## 2. Préparation de la carte MicroSD (FAT32)

Le moteur SCUMM recherche un fichier marqueur `.scumm` dans le dossier `/cart/` :

1. Copiez les fichiers de votre jeu dans un dossier, par exemple :
   `/scumm/loom/` (ou `/scumm/monkey/`)
2. Créez un fichier texte dans `/cart/`, par exemple `/cart/loom.scumm`, contenant une seule ligne avec le chemin du dossier :
   ```text
   /scumm/loom
   ```
   *(Si le fichier `/cart/nom.scumm` est vide, l'émulateur cherchera dans `/cart/nom/`.)*

# Gérer et choisir plusieurs jeux SCUMM sur la carte MicroSD

L'émulateur ne propose pas de menu de sélection graphique au démarrage : il lance automatiquement **le premier fichier `.scumm` trouvé par ordre alphabétique** dans le dossier `/cart/`.

Vous pouvez stocker autant de jeux que vous le souhaitez dans `/scumm/` (ex. `/scumm/loom/`, `/scumm/monkey/`, `/scumm/indy3/`) et utiliser l'une des deux méthodes suivantes pour choisir lequel lancer.

---

## Méthode 1 : Désactiver les autres marqueurs (Recommandée)

Ne gardez qu'un seul fichier actif se terminant par `.scumm` dans `/cart/` et ajoutez `.off` (ou `.bak`) à la fin des autres :

```text
/cart/loom.scumm            <-- Actif : ce jeu se lancera au démarrage
/cart/monkey.scumm.off      <-- Ignoré par l'émulateur
/cart/indy3.scumm.off       <-- Ignoré par l'émulateur
```

* **Pour changer de jeu :** Branchez la carte SD sur votre ordinateur, renommez `loom.scumm` en `loom.scumm.off`, puis renommez `monkey.scumm.off` en `monkey.scumm`.

---

## Méthode 2 : Jouer sur l'ordre alphabétique

Si vous laissez plusieurs fichiers `.scumm` simultanément dans `/cart/`, le moteur chargera uniquement celui qui arrive en premier dans l'ordre alphabétique (par exemple, `indy3.scumm` sera toujours lancé avant `loom.scumm`).

* **Pour forcer un jeu à démarrer :** Préfixez son fichier marqueur avec un chiffre ou un caractère prioritaire pour le placer en tête de liste :

```text
/cart/0_monkey.scumm        <-- Passe en premier : se lance au démarrage
/cart/indy3.scumm
/cart/loom.scumm
```

* **Pour revenir à un autre jeu :** Retirez simplement le préfixe `0_` (ou ajoutez-le devant un autre fichier `.scumm`).


> **Écrans de diagnostic au démarrage :**
> - **Rouge** : carte MicroSD absente ou illisible.
> - **Jaune** : aucun fichier `.scumm` dans `/cart/` ou dossier de jeu vide.
> - **Magenta** : jeu non reconnu ou erreur mémoire PSRAM.

---

## 3. Contrôles & Menu OSD (`F4`)

| Périphérique | Action |
| :--- | :--- |
| **Souris USB (via Hub ou direct)** | Déplacement du pointeur + Clic gauche / Clic droit |
| **Clavier USB / PS/2** | Saisie clavier complète (`F1`–`F12`, `F5` Sauvegarde/Chargement, `Espace` Pause, `Échap` Passer cinématique, `.` Passer dialogue) |
| **F4** *(ou **Select+Start** NES)* | Ouvre le **Menu OSD** (Sortie Audio `HDMI / I2S / PWM`, Volume `25%–100%`, Vitesse pointeur) |
| **Manette NES / USB** | **D-Pad** = Pointeur, **A** = Clic gauche, **B** = Clic droit, **Start** = Espace, **Select** = `F5` |

---

## 4. Brochage GPIO (Waveshare RP2350-PiZero)

| Fonction | GPIO RP2350 | Détails |
| :--- | :--- | :--- |
| **HDMI / DVI (`PIO0`)** | `32..39` | `D2-/+` (32/33), `D1-/+` (34/35), `D0-/+` (36/37), `CLK-/+` (38/39) |
| **MicroSD (`SPI1`)** | `30, 31, 40, 43` | `SCK=30`, `MOSI=31`, `MISO=40`, `CS=43` |
| **PSRAM `8 Mo`** | `47` | `CS1` (QSPI XIP `0x11000000`) |
| **PIO-USB Host** | `28` (`D+`), `29` (`D-`) | Second port USB Host (supporte Hub, Souris, Clavier, Manette) |
| **Clavier PS/2 (`PIO2`)** | `16` (`DATA`), `17` (`CLK`) | Pull-ups `4,7 kΩ` vers `3,3V` |
| **Audio I2S** | `18, 19, 21` | `BCK=18`, `LRCK=19`, `DIN=21` |
| **Audio PWM** | `26` | Sortie PWM `96 kHz` |
| **Manette NES** | `5, 6, 9` | `CLK=5`, `DATA=6`, `LATCH=9` (`3,3V`) |

---

## 5. Compilation et Flashage (PlatformIO)

```bash
cd scumm
pio run
```

- Firmware généré : [scumm_rp2350_pizero.uf2](file:///home/alecren/Documents/oric/scumm/scumm_rp2350_pizero.uf2)
- Maintenez **BOOT** en branchant la carte RP2350-PiZero puis glissez-déposez `scumm_rp2350_pizero.uf2` sur le lecteur `RP2350`.

# Archives & Collections de Jeux Rétro PC (LucasArts, ScummVM & MS-DOS)

Ce document recense des ressources et collections d'archives pour les jeux d'aventure classiques, les images de disquettes LucasArts et les compilations compatibles ScummVM et DOSBox.

---

## 6. Documentation & Images Disquettes LucasArts
* **Source :** [LucasArts PC Floppy Disk Image Collection (Internet Archive)](https://archive.org/details/20220917_20220917_0541)
* **Taille totale :** 9,6 Go (156 titres dont 43 uniques, éditions US/Canada et internationales : France, Allemagne, Italie, Espagne, Royaume-Uni, etc.)
* **Contenu :**
  * Images disquettes PC (360K, 720K, 1.2M, 1.44M) et dumps KryoFlux vérifiés (`[Flux]`, `[!]`).
  * Archives de documentation complètes (`- Docs.zip` : manuels, guides, encarts) pour des classiques tels que *Monkey Island 1 & 2*, *Indiana Jones (Last Crusade, Fate of Atlantis)*, *Day of the Tentacle*, *Sam & Max Hit the Road*, *Loom*, *Maniac Mansion*, *Zak McKracken*, *X-Wing* et *TIE Fighter*.
  * Fichiers de référence inclus : `NamingScheme.txt` et `forensics.txt`.

---

## 7. Jeux ScummVM (ROMs & Collections Complètes)

### Catalogue en ligne (Français)
* **Source :** [ROMs ScummVM sur RetroVault](https://retrovault.ch/console/scummvm?page=5&lang=fr)
* **Contenu :** Répertoire de 319 jeux compatibles avec l'interpréteur ScummVM (incluant *King's Quest*, *Leisure Suit Larry*, *Laura Bow*, *Indiana Jones*, *Living Books*, *Lands of Lore*, etc.).

### Archive complète v2.0
* **Source :** [The Complete ScummVM Collection v2.0 (Internet Archive)](https://ia601608.us.archive.org/view_archive.php?archive=/22/items/The_Complete_ScummVM_Collection_v2/The_Complete_ScummVM%20Collection%20v2.0.zip)
* **Structure de l'archive :**
  * `Working/` : Jeux fonctionnels prêts pour ScummVM (versions Floppy, CD, DVD, DOS, Windows, Mac, Amiga, Atari ST, FM Towns).
  * `Alternate Languages/` : Versions localisées (Français, Allemand, Espagnol, Italien, etc.) et traductions de fans (`!Fan Translations.zip`).
  * `Documentation/` : Manuels (`Manuals.zip`), posters (`Posters.zip`), guides multilingues et ROMs Roland MT-32 (`Roland MT-32 ROMs.zip`).
  * `Enhanced Music/` : Bandes-son améliorées (Mono/Stereo) et guide d'installation (`Enhanced Music Install.txt`).
  * `Fanmade AGI Games/` & `SVN/` : Créations amateurs sous moteur AGI et titres additionnels.

---

## 8. Collection MS-DOS Préconfigurée pour DOSBox
* **Source :** [Retro Dos (Floppy Disk) Games Collection (Internet Archive)](https://archive.org/details/retro-dos-games-collection)
* **Taille totale :** 1,2 Go (répartis en 24 fichiers images ISO classés par ordre alphabétique de `#-A.iso` à `Z.iso`).
* **Contenu :** Sélection de jeux MS-DOS (1983–1996) incluant des titres d'action, de plateforme, de simulation et d'aventure (*Doom*, *Duke Nukem*, *Prince of Persia*, *Monkey Island*, *King's Quest*, *Space Quest*, *Lemmings*, *Warcraft*, *Transport Tycoon*, *Syndicate*, etc.).
* **Utilisation :**
  1. Télécharger et monter/extraire l'image `.iso` souhaitée.
  2. Décompresser l'archive `.rar` du jeu dans un dossier dédié.
  3. Exécuter le fichier `!start.bat` pour lancer le jeu automatiquement via DOSBox.


