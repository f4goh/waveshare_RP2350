# Aide-mémoire `pico-infonesPlus` (Waveshare RP2040 / RP2350-PiZero)

Guide rapide des commandes clavier/manettes et du câblage matériel sur le connecteur 40 broches de la carte **Waveshare RP2040 / RP2350-PiZero**.

---

## 1. Correspondance des touches (Clavier vs Manettes)

### Navigation dans les menus (Liste des ROMs)

| Action | Clavier AZERTY | Clavier QWERTY | Manette NES / SNES | Manette Xbox / PS |
| :--- | :--- | :--- | :--- | :--- |
| **Haut / Bas** | `↑` / `↓` | `↑` / `↓` | Croix Haut / Bas | Croix Haut / Bas |
| **Page précédente / suivante** | `←` / `→` | `←` / `→` | Croix Gauche / Droite | Croix Gauche / Droite |
| **Ouvrir dossier / Lancer jeu** | `X` | `X` | `A` (NES) / `B` (SNES) | `B` (Xbox) / `Rond` (PS) |
| **Dossier parent / Retour** | `W` | `Z` | `B` (NES) / `Y` (SNES) | `A` (Xbox) / `Croix` (PS) |
| **20 derniers jeux lancés** | `C` | `C` | `X` *(SNES uniquement)* | `Y` (Xbox) / `Triangle` (PS) |
| **Infos jeu & jaquette (Metadata)** | `S` | `S` | `START` | `START` / `Options` |
| **Menu Paramètres (Settings)** | `Q` | `A` | `SELECT` | `SELECT` / `Share` |

### Commandes en cours de partie (In-Game)

| Action | Clavier AZERTY | Clavier QWERTY | Manette NES / SNES | Manette Xbox / PS |
| :--- | :--- | :--- | :--- | :--- |
| **Bouton B (NES)** | `W` | `Z` | `B` (NES) / `Y` (SNES)\* | `A` (Xbox) / `Croix` (PS)\* |
| **Bouton A (NES)** | `X` | `X` | `A` (NES) / `B` (SNES)\* | `B` (Xbox) / `Rond` (PS)\* |
| **SELECT** | `Q` | `A` | `SELECT` | `SELECT` / `Share` |
| **START** | `S` | `S` | `START` | `START` / `Options` |
| **Menu Pause / Sauvegarde / Quitter** | `Q` + `S` | `A` + `S` | `SELECT` + `START` | `SELECT` + `START` |
| **Sauvegarde rapide (Quick Save)** | `S` + `↓` | `S` + `↓` | `START` + Bas | `START` + Bas |
| **Chargement rapide (Quick Load)** | `S` + `↑` | `S` + `↑` | `START` + Haut | `START` + Haut |
| **Changer ratio écran (Screen Mode)** | `Q` + `↑` / `↓` | `A` + `↑` / `↓` | `SELECT` + Haut / Bas | `SELECT` + Haut / Bas |
| **Changer palette de couleurs** | `S` + `←` / `→` | `S` + `←` / `→` | `START` + Gauche / Droite | `START` + Gauche / Droite |
| **Afficher / Masquer les FPS** | `S` + `X` | `S` + `X` | `START` + `A` (NES) | `START` + `B` (Xbox) |
| **Enregistrer audio WAV (30s)**\*\* | `S` puis `S` + `W` | `S` puis `S` + `Z` | `START` puis `START` + `B` | `START` puis `START` + `A` |
| **Redémarrer en mode BOOTSEL** | `Q` + `S` + `↑` + `X` | `A` + `S` + `↑` + `X` | `SELECT` + `START` + Haut + `A` | `SELECT` + `START` + Haut + `B` |

> \* **Note :** Correspond au réglage **Button Layout = SNES** dans les paramètres (recommandé pour les manettes à 4 boutons en losange).  
> \*\* **Audio WAV :** Uniquement sur **RP2350 avec PSRAM** (enregistre dans `/soundrecorder.wav` à la racine de la carte SD).

---

## 2. Câblage GPIO : Manettes NES / SNES (1 ou 2 joueurs)

Compatible avec les manettes originales **NES** et **SNES** (détection automatique au branchement).

> ⚠️ **Important :** Sur la Waveshare RP2040 / RP2350-PiZero, relier obligatoirement **VCC au 5V** (et non au 3,3V), sinon les manettes risquent de ne pas répondre.

| Signal Manette | Port 1 (Joueur 1) | Pin physique (Port 1) | Port 2 (Joueur 2) | Pin physique (Port 2) |
| :--- | :--- | :--- | :--- | :--- |
| **GND (Masse)** | `GND` | Pin 6, 9, 14, 20, 25... | `GND` | Pin 6, 9, 14, 20, 25... |
| **VCC (Alimentation)** | `5V` | Pin 2 ou Pin 4 | `5V` | Pin 2 ou Pin 4 |
| **Clock (Horloge)** | `GPIO 5` | Pin 29 | `GPIO 10` | Pin 19 |
| **Latch (Verrouillage)** | `GPIO 9` | Pin 21 | `GPIO 11` | Pin 23 |
| **Data (Données)** | `GPIO 6` | Pin 31 | `GPIO 12` | Pin 32 |

---

## 3. Câblage GPIO : Sortie Audio I2S (DAC externe PCM5102A / TDA1387)

Permet d'ajouter une sortie casque / ligne analogique de haute qualité. Pour l'activer, sélectionnez **External Audio = ON** dans le menu *Settings* (pensez à cliquer sur **SAVE**).

| Broche Module DAC | Broche Waveshare PiZero | Pin physique (Header 40 pins) | Fonction |
| :--- | :--- | :--- | :--- |
| **VCC / VIN** | `3V3` *(ou `5V` selon module)* | Pin 1 (`3V3`) ou Pin 2 (`5V`) | Alimentation du DAC |
| **GND** | `GND` | Pin 6, 9, 14, 20, 25, 30, 34 ou 39 | Masse commune |
| **BCK / BCLK** | `GPIO 18` | Pin 12 | Bit Clock (Horloge bit) |
| **LCK / WS / LRCK** | `GPIO 19` | Pin 35 | Word Select / Left-Right Clock |
| **DIN / DATA** | `GPIO 21` | Pin 40 | Données audio I2S |
| **SCK / MCK** *(si présent)* | `GND` | Masse | Horloge maître (générée en interne sur PCM5102A) |

