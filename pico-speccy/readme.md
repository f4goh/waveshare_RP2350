# ZX Spectrum — `pico-speccy` (Waveshare RP2350-PiZero)

Guide complet d'installation, raccourcis clavier et réseau Wi-Fi pour l'émulateur **Sinclair ZX Spectrum** (`pico-speccy`) sur la carte **Waveshare RP2350-PiZero**.

* **Projet principal :** [drewpo28/pico-speccy](https://github.com)
* **Téléchargements :** [Page des Releases](https://github.com/releases)
* **Firmware recommandé :** `z0p2-speccy-VGA-HDMI-PIOUSB-16MB-1.1.3.uf2` *(ou `1.1.2`)*
  * **`PIOUSB` :** active le second port USB-C (`J2`) en mode hôte pour brancher simultanément clavier et manette ou clé USB.
  * **`16MB` :** tire parti des 16 Mo de Flash de la carte pour embarquer la ROM MoonSound (YRW801) et une banque sonore GM.DLS.
* **Wiki réseau :** [Documentation EN-Network](https://github.com/wiki/EN-Network)

---

## 1. Préparer la carte microSD et lancer des jeux

### Préparation du support
* **Formatage :** Formatez votre carte microSD en **FAT32**.
* **Organisation :** Créez vos propres répertoires (`/Jeux`, `/Next`, `/Demos`) ou placez vos fichiers à la racine.
* **Clé USB (`PIOUSB`) :** Tout support USB connecté au port `J2` apparaît automatiquement sous le lecteur `USB:` dans le navigateur `F5`.

### Démarrer un programme
1. Ouvrez l'explorateur de fichiers avec **`F5`**.
2. Sélectionnez le fichier voulu et validez avec **`Entrée`**.
   * *Astuce :* Les archives `.zip` et `.gz` sont explorées et décompressées à la volée.

### Formats pris en charge
* **Instantanés mémoire :** `.z80`, `.sna`, `.pss`, `.spg`, `.rzx`
* **Cassettes :** `.tap`, `.tzx`, `.pzx`, `.csw`
* **Disquettes et disques durs :** `.trd`, `.scl`, `.udi`, `.fdi`, `.dsk`, `.mbd`, `.img`, `.hdf`, `.vhd`
* **Cartouches :** `.rom`, `.dck`
* **ZX Spectrum Next *(puce PSRAM 8 Mo requise)* :** `.nex`, `.snx`, `.bas`

---

## 2. Configurer le système ZX Spectrum Next (NextZXOS)

Pour émuler le **ZX Spectrum Next** et démarrer sur son environnement graphique **NextZXOS** (en plus d'une puce **PSRAM de 8 Mo** soudée au dos de la carte), vous devez copier la distribution système officielle sur votre carte microSD :

1. Rendez-vous sur le portail officiel de téléchargement : [SpecNext Latest Distro](https://specnext.com).
2. Téléchargez l'archive de distribution sans code source : **`sn-nosrc-26.11a.zip`** *(ou la version la plus récente disponible)*.
3. Décompressez l'intégralité du contenu de cette archive directement **à la racine** de votre carte microSD (vous devez y retrouver les dossiers `nextzxos`, `sys`, `dot`, `demos`, etc.).
4. Dans l'émulateur, appuyez sur **`F1`**, allez dans **`Machine`** puis choisissez **`Spectrum Next`** pour démarrer le système.

---

## 3. Installer des ROMs système personnalisées (Optionnel)

Les ROMs d'origine (48K, 128K, +2, +3, Pentagon, etc.) sont intégrées au binaire. Pour utiliser des ROMs modifiées (ex. *Rodolfo Guerra* pour le chargement rapide) :

1. Placez les fichiers **à la racine** de la carte microSD sous les noms exacts :
   * **`48custom.rom`** : taille stricte de **16 384 octets** (16 Ko).
   * **`128custom.rom`** : taille de **16 Ko** ou **32 768 octets** (32 Ko).
2. Ouvrez le menu principal avec **`F1`**, entrez dans **`Update`**, puis sélectionnez **`Custom ROM 48K`** ou **`Custom ROM 128K`**.
3. Laissez ces deux fichiers sur la carte SD afin de pouvoir les réappliquer après chaque mise à jour du firmware `.uf2`.

---

## 4. Raccourcis Clavier par défaut
*(Reconfigurables dans le menu `F1`, à l'exception de `F1` et `ALT + F1`)*

### Gestion des fichiers, menus et sauvegardes
* **`F1`** : Ouvrir le menu principal (ou afficher l'aide contextuelle dans les sous-menus).
* **`F2`** : Chargement direct d'un instantané ou exécutable (`.pss`, `.sna`, `.z80`, `.nex`, `.snx`, etc.).
* **`F3` / `F4`** : Ouvrir les 40 emplacements pour charger (`F3`) ou sauvegarder (`F4`) un état `.pss`.
* **`F5`** : Ouvrir le gestionnaire de fichiers (`SD:`, `USB:`, `FTP/SFTP`, `Web Archives`).
* **`ALT + F3` / `ALT + F4`** : Chargement rapide (`ALT + F3`) ou sauvegarde rapide (`ALT + F4`).
* **`ALT + F7`** : Accès au menu rapide (`0` Machines, `1` Profils, `2` Joysticks, `3` Périphériques, `4` Vidéo, `5` Audio, `6` CPU).

### Lecteur de cassettes (`.tap`, `.tzx`, `.pzx`, `.csw`)
* **`F6`** : Lancer ou mettre en pause la lecture de la bande.
* **`F7`** : Ouvrir le navigateur de blocs (`F8` dans cette fenêtre pour éjecter la cassette).

### Contrôle système et matériel
* **`F9` / `F10`** : Diminuer (`F9`) ou augmenter (`F10`) le volume audio.
* **`F11`** : Réinitialisation à chaud de la machine émulée (préserve la RAM).
* **`F12`** : Redémarrage matériel complet du RP2350.
* **`~` (Tilde)** : Basculer en mode vitesse maximale.
* **`Pause`** : Suspendre l'émulation.
* **`ALT + F2`** : Changer la cadence **Turbo Z80** (3,5 / 7 / 14 / 28 MHz).
* **`ALT + F10`** : Déclencher le bouton **NMI / Magic Button** (ou bouton Drive / Multiface sur Next).
* **`ALT + F11`** : Redémarrage ciblé (Service ROM, TR-DOS, 128K, 48K ou CP/M selon l'ordinateur choisi).
* **`ALT + F12`** : Basculer la carte en mode **BOOTSEL** pour flasher un firmware.
* **`R` *(maintenu au démarrage)*** : Restauration complète des paramètres d'usine.

### Affichage, débogage et contrôles en jeu
* **`F8`** : Afficher ou masquer les statistiques CPU et FPS.
* **`ALT + F1`** : Consulter les informations matérielles (température, horloges, état PSRAM).
* **`ALT + F5`** : Lancer le débogueur Z80/Z80N.
* **`ALT + F8`** : Afficher ou masquer les voyants LED d'activité (lecteur disquette, SD, disque dur, MIDI).
* **`ALT + F9`** : Saisir un code **POKE**.
* **`ALT + PageUp`** : Faire défiler les modes **Gigascreen** (`Off` → `On` → `Auto`).
* **`Impr. Écran` (`PrntScr`)** : Enregistrer une capture d'écran `.bmp` dans `/pico-speccy/screenshots`.
* **`W A S D` / `K L`** : Émuler un joystick **Kempston** depuis le clavier.

---

## 5. Connexion Wi-Fi avec un module ESP-01S

### 5.1 Récupérer le firmware AT (1 Mo)
* **Dépôt source :** [CytronTechnologies/esp-at-binaries](https://github.com)
* **Binaire à télécharger :** `Cytron_ESP-01S_AT_Firmware_V2.2.0.0.bin` *(image complète compatible SSL/TLS pour les catalogues en ligne, à écrire à l'adresse `0x00000`)*.

### 5.2 Programmer l'ESP-01S sous Linux (`esptool`)

#### Branchement sur adaptateur USB-TTL (3,3 V strict)
* **3V3** → `3.3V`
* **EN (CH_PD)** → `3.3V`
* **GND** → `GND`
* **TX** → `RX`
* **RX** → `TX`
* **GPIO 0** → **`GND`** *(à relier **avant** de connecter l'adaptateur USB au PC pour entrer en mode flash)*

#### Commandes de flashage
```bash
# Autoriser l'accès au port série
sudo chmod 666 /dev/ttyUSB0

# Vérifier l'identification de la puce et sa capacité (1MB)
esptool.py --port /dev/ttyUSB0 flash_id

# Effacer la mémoire Flash
esptool.py --port /dev/ttyUSB0 erase_flash

# Écrire le firmware AT complet à l'adresse 0x0
esptool.py --port /dev/ttyUSB0 --baud 115200 write_flash --flash_mode dout --flash_size 1MB 0x0 Cytron_ESP-01S_AT_Firmware_V2.2.0.0.bin
```

> *Une fois l'opération terminée, débranchez l'adaptateur, retirez le fil entre `GPIO 0` et `GND`, puis rebranchez : l'envoi de `AT` à 115200 bauds doit renvoyer `OK`.*

### 5.3 Raccordement sur la Waveshare RP2350-PiZero (4 fils)
Branchez le module sur le port UART par défaut (`GPIO 24 / GPIO 25`, compatible simultanément avec le DAC audio I2S PCM5122) :
* **3V3 (ESP-01S)** → **`3V3`** (Pin physique `1` ou `17`)
* **GND (ESP-01S)** → **`GND`** (Pin physique `6`, `9`, `14`, `20`...)
* **TX (ESP-01S)** → **`GPIO 25` (RX)** (Pin physique `22`)
* **RX (ESP-01S)** → **`GPIO 24` (TX)** (Pin physique `18`)

*(Les broches `EN`, `RST`, `GPIO 0` et `GPIO 2` restent libres en usage normal. Vous pouvez également relier l'ESP-01S via un dongle USB-Série CH340/CP2102 sur le port USB).*

### 5.4 Paramétrer le Wi-Fi dans `pico-speccy`
1. Appuyez sur **`F1`** puis ouvrez le menu **`Network`**.
2. Dans **`Transport`**, sélectionnez **`GPIO 24/25`** *(ou `USB (CH340)` avec un adaptateur USB)*.
3. Ouvrez **`WiFi`**, lancez un **Scan**, choisissez votre réseau (SSID) et entrez votre clé Wi-Fi.
4. Activez **`Autoconnect`** et sauvegardez la configuration pour automatiser la connexion et la mise à l'heure **SNTP** à chaque démarrage.

#### Services accessibles en ligne
* **Transfert de fichiers par FTP :** Démarrez **`FTP server`** dans le menu `Network`, relevez l'adresse IP affichée et connectez-vous depuis votre ordinateur (mode **Anonyme** et **Actif** dans FileZilla) pour copier vos jeux sur la carte SD.
* **Catalogues en ligne :** Appuyez sur **`F5`** et sélectionnez **`Web Archives`** pour télécharger et lancer directement des jeux depuis les bibliothèques ZX Spectrum (*Virtual TR-DOS, Spectrum Computing, ZX-Art, VGMRips*, etc.).

