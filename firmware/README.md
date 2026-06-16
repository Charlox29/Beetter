# Beetter Hive – Firmware & Documentation

Firmware complet pour le module **Beetter Hive** (ESP32-C6-DevKitC-1-N8)
et le récepteur Python pour **Beetter Home** (Raspberry Pi).

---

## Structure du projet

```
Beetter/
├── Beetter_Main/
│   ├── Beetter_Main.ino     ← Programme principal (ouvrir dans Arduino IDE)
│   ├── BeetterConfig.h      ← ⭐ FICHIER À ÉDITER en premier
│   └── src/                 ← Bibliothèques locales (même dossier que .ino)
│       ├── BeetterLoRa.cpp/.h    Transmission LoRa binaire (Grove RFM95)
│       ├── BeetterMic.cpp/.h     Microphones I2S + FFT + MFCC
│       ├── BeetterPhoto.h        Photorésistance GL5539
│       ├── BeetterRTC.cpp/.h     Horloge temps réel PCF8523
│       ├── BeetterSD.cpp/.h      Carte micro-SD (ADA4682)
│       ├── BeetterSHT40.cpp/.h   Capteurs T°/Humidité (×2)
│       ├── BeetterWAV.cpp/.h     Enregistrement audio WAV
│       └── BeetterWifi.h         WiFi + Bluetooth LE
├── grove_lora.py            ← Driver bas niveau Grove (requis par receiver.py)
├── receiver.py              ← Récepteur LoRa côté Beetter Home (Pi)
└── README.md
```

---

## Installation Arduino IDE

### 1. Bibliothèques externes (Library Manager)

`Outils → Gérer les bibliothèques`

| Bibliothèque | Version min |
|---|---|
| `Adafruit SHT4x` | 1.0.0 |
| `Adafruit BusIO` | 1.14.0 |
| `RTClib` (by Adafruit) | 2.0.0 |
| `arduinoFFT` (by Enrique Condes) | **2.0.0 obligatoire** |

### 2. Bibliothèque Grove LoRa (ZIP)

```
https://github.com/Seeed-Studio/Grove_LoRa_433MHz_and_915MHz_RF
→ Code → Download ZIP
→ Arduino IDE : Croquis → Inclure une bibliothèque → Ajouter la bibliothèque .ZIP
```

⚠️ Si `RadioHead` est déjà installée, la désinstaller avant (conflit sur `RH_RF95.h`).

### 3. Configuration de la carte

| Paramètre | Valeur |
|---|---|
| Carte | `ESP32C6 Dev Module` |
| Core arduino-esp32 | `>= 3.0.0` |
| Flash Size | `8MB (64Mb)` |
| Partition Scheme | `8M with spiffs (3MB APP/1.5MB SPIFFS)` |

URL boards manager :
`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

### 4. Configurer et flasher

1. Ouvrir `Beetter_Main/Beetter_Main.ino`
2. Éditer `BeetterConfig.h` (identifiant ruche, WiFi, fuseau horaire)
3. Compiler et flasher — le programme démarre automatiquement sans moniteur série

---

## Brochage ESP32-C6

| Composant | Broche(s) | Bus |
|---|---|---|
| SHT40 EXTÉRIEUR (J1) | SDA=GPIO6, SCL=GPIO7 | I2C matériel |
| SHT40 INTÉRIEUR (J3) | SDA=GPIO10, SCL=GPIO11 | I2C logiciel (bit-bang) |
| RTC PCF8523 | SDA=GPIO6, SCL=GPIO7 | I2C matériel (partagé avec SHT40 EXT) |
| Micro EXTÉRIEUR (J2, SEL=GND) | BCLK=GPIO2, WS=GPIO1, DIN=GPIO3 | I2S canal gauche |
| Micro INTÉRIEUR (J5, SEL=3.3V) | Partagé BCLK/WS/DIN | I2S canal droit |
| Micro-SD ADA4682 | SCK=18, MISO=20, MOSI=19, CS=21, DET=22 | SPI |
| LoRa Grove RFM95 | RX=GPIO5, TX=GPIO4 | UART1 @ 57600 bauds |
| Photorésistance GL5539 | GPIO0 | ADC 12 bits |
| LED RGB WS2812 | GPIO8 | neopixelWrite() |

---

## Architecture du firmware

### Cycle séquentiel (toutes les 30s)

```
┌─────────────────────────────────────────────────────────────────┐
│  Cycle complet (~9.5s actif + ~20.5s attente)                   │
│                                                                  │
│  1. SHT40 INT + EXT      ~20 ms   T°, %RH, humidité absolue    │
│  2. Photorésistance        ~5 ms   ADC brut + indice 0.0–10.0   │
│  3. MFCC intérieur      ~3050 ms   11 fenêtres × 2048 samples  │
│  4. MFCC extérieur      ~3050 ms   11 fenêtres × 2048 samples  │
│  5. Écriture SD           ~15 ms   3 fichiers CSV (1 ouverture) │
│  6. Envoi LoRa           ~176 ms   ENV+AUD 96 bytes, DC 0.60%  │
│  7. [WAV si cycle×20]  ~15 000 ms   clip stéréo 15s sur SD     │
│  8. Attente               reste    jusqu'à t=30s depuis début   │
└─────────────────────────────────────────────────────────────────┘
```

Tout ce qui est mesuré est envoyé — pas de mesure inutile.

### Démarrage

Le programme démarre automatiquement à la mise sous tension.
Pas de `while(!Serial)` — aucune dépendance au moniteur série.

### LED RGB (GPIO8)

| Couleur | Moment | Signification |
|---|---|---|
| Vert 500ms | Fin du setup() | Initialisation OK |
| Bleu (flash) | Envoi LoRa | Transmission en cours |

---

## MFCC – Analyse acoustique

### Pipeline

```
Capture I2S (3s × 8000 Hz = 24 000 samples, buffer statique 94 KB)
        ↓  réinitialisation I2S avant chaque capture (stabilité bus)
  ┌──────────────────────────────────────────────┐
  │  11 fenêtres × 2048 samples (256 ms chacune) │
  │  Pour chaque fenêtre :                        │
  │    RMS → accumulateur                         │
  │    Fenêtre de Hamming → FFT → magnitude       │
  │    Fréquence dominante → accumulateur         │
  │    26 filtres Mel → log énergie → DCT         │
  │    MFCC[13] → accumulateur                    │
  └──────────────────────────────────────────────┘
        ↓ moyenne des 11 accumulateurs
  RMS, freqDominante, MFCC[13] représentatifs de 3s
```

### Cohérence avec l'entraînement ML

Le fenêtrage + moyennage est **identique au comportement par défaut de `librosa.feature.mfcc()`**. Les vecteurs produits par le firmware sont directement comparables aux données d'entraînement.

### Mémoire

| Élément | Taille | Nature |
|---|---|---|
| `_captureBuf[24000]` | 94 KB | Statique global — alloué au démarrage |
| `_vReal[2048]` + `_vImag[2048]` | 32 KB | Statique membre — réutilisés par fenêtre |
| Accumulateurs moyennage | ~120 bytes | Stack — négligeable |

Les micros INT et EXT partagent `_captureBuf` séquentiellement : INT capturé → analysé → buffer écrasé par EXT. **48 000 samples capturés par cycle, 94 KB en mémoire.**

### Paramètres

| Paramètre | Valeur | Description |
|---|---|---|
| `MIC_INT_SAMPLE_RATE_HZ` | 8000 | Fréquence d'échantillonnage (Hz) |
| `MIC_INT_FREQ_MIN_HZ` | 50 | Borne basse de la plage d'intérêt |
| `MIC_INT_FREQ_MAX_HZ` | 2000 | Borne haute – couvre harmoniques H1–H8 abeille |
| `MIC_INT_CAPTURE_MS` | 3000 | Durée de capture en ms |
| `MFCC_NUM_COEFFS` | 13 | Coefficients cepstraux |
| `MFCC_NUM_FILTERS` | 26 | Filtres Mel (banque standard) |

Signatures acoustiques couvertes (50–2000 Hz) :

| Comportement | Fréquence |
|---|---|
| Colonie calme | 200–250 Hz |
| Ventilation | 200–230 Hz |
| Stress léger | 300–400 Hz |
| Essaimage imminent | 350–500 Hz |
| Orphelinage | 300–450 Hz |
| Intrusion frelon asiatique | 400–600 Hz |
| Harmoniques H2–H8 abeille | 460–1840 Hz |

---

## Photorésistance GL5539

### Modèle physique

Diviseur de tension : `3.3V ── R_fixe(10kΩ) ── GPIO0 ── R_photo ── GND`

```
ADC brut (0..4095)
  → R_ldr = R_FIXE × (4095 - ADC) / (ADC + 1)
  → Lux   = (LDR_A / R_ldr)^(1/LDR_GAMMA)
  → Indice = log10(1 + Lux) / log10(1 + LUX_MAX) × 10
```

| Paramètre | Valeur | Description |
|---|---|---|
| `R_FIXE_OHM` | 10 000 Ω | Résistance du diviseur de tension |
| `LDR_GAMMA` | 0.7 | Exposant GL5539 (datasheet) |
| `LDR_A` | 297 957 | Calibré : ADC=2642 = 300 lux (bureau éclairé) |
| `LUX_MAX` | 100 000 lux | Référence 10.0/10 (plein soleil direct) |

L'indice est **logarithmique** pour refléter la perception humaine. Table de correspondance mesurée :

| Situation | ADC mesuré | Lux calculé | Indice |
|---|---|---|---|
| Bureau éclairé (point de calibration) | 2642 | 300 lux | 5.0/10 |
| LED téléphone à 20 cm | 3157 | 1101 lux | 6.1/10 |
| À l'ombre intérieure | 1073 | 44 lux | 3.3/10 |
| Luminosité naturelle intérieure | 1996 | 181 lux | 4.5/10 |

---

## Protocole LoRa – Blocs binaires

Une trame = 2 blocs concaténés = **96 bytes**, toutes les 30s.
DC pratique : 176ms / 30 000ms = **0.60%** ✓ (limite ETSI 868 MHz = 1%)

### BLOC ENV – `0xE0` – 23 bytes

| Offset | Bytes | Champ | Encodage |
|---|---|---|---|
| 0 | 1 | magic | `0xE0` |
| 1 | 2 | seq | uint16 (tronqué depuis uint32) |
| 3 | 4 | hive_id | `"B001"` ASCII |
| 7 | 4 | timestamp | uint32 Unix UTC |
| 11 | 2 | temp_int | int16 = °C × 100 |
| 13 | 2 | hum_int | uint16 = %RH × 10 |
| 15 | 2 | temp_ext | int16 = °C × 100 |
| 17 | 2 | hum_ext | uint16 = %RH × 10 |
| 19 | 2 | light_ext | uint16 = indice × 10 (ex. 50 → 5.0/10) |
| 21 | 2 | crc16 | CRC-16/CCITT (poly 0x1021, init 0xFFFF) |

### BLOC AUD – `0xA0` – 73 bytes

| Offset | Bytes | Champ | Encodage |
|---|---|---|---|
| 0 | 1 | magic | `0xA0` |
| 1 | 2 | seq | uint16 |
| 3 | 4 | hive_id | `"B001"` ASCII |
| 7 | 4 | timestamp | uint32 Unix UTC |
| 11 | 2 | freq_int | uint16 = Hz × 10 |
| 13 | 2 | rms_int | uint16 = RMS × 10 000 |
| 15 | 2 | freq_ext | uint16 = Hz × 10 |
| 17 | 2 | rms_ext | uint16 = RMS × 10 000 |
| 19 | 26 | mfcc_int[13] | int16[13] = coefficient × 100 |
| 45 | 26 | mfcc_ext[13] | int16[13] = coefficient × 100 |
| 71 | 2 | crc16 | CRC-16/CCITT |

`freq` et `rms` sont des **moyennes sur 11 fenêtres** (cohérent avec les MFCC).

---

## Micro-SD – Fichiers produits

### CSV (un enregistrement par cycle de 30s)

| Fichier | En-tête | Volume |
|---|---|---|
| `/env.csv` | `timestamp, T_int_C, H_int_RH, Ha_int_gm3, T_ext_C, H_ext_RH, Ha_ext_gm3, lum_brut, lum_indice` | ~70 bytes/ligne |
| `/mfcc_int.csv` | `timestamp, freq_dom_Hz, rms, mfcc0..mfcc12` | ~135 bytes/ligne |
| `/mfcc_ext.csv` | `timestamp, freq_dom_Hz, rms, mfcc0..mfcc12` | ~135 bytes/ligne |

Volume total CSV : ~340 bytes/cycle → **~1 MB/jour → ~44 ans sur 16 Go**.

L'en-tête est réécrit automatiquement si le fichier est **absent ou vide**.
Les 3 fichiers sont ouverts en une seule opération par cycle (`beginCycle/endCycle`).

### WAV (audio brut stéréo)

Format : **PCM 16 bits, stéréo, 8000 Hz**
Canal gauche (L) = micro EXTÉRIEUR (J2) | Canal droit (R) = micro INTÉRIEUR (J5)

Atténuation : `WAV_GAIN_SHIFT = 3` → -18 dB (adapté au bruit de ruche)

| Paramètre | Valeur | Description |
|---|---|---|
| `WAV_EVERY_CYCLES` | 20 | 1 clip toutes les 10 min (20 × 30s) |
| `WAV_DUREE_SEC` | 15 | Durée du clip en secondes |
| `WAV_GAIN_SHIFT` | 3 | Atténuation : -18 dB |

Volume WAV : 0.46 MB/clip × 144 clips/jour = **66 MB/jour → 249 jours sur 16 Go**

Nom de fichier : `/wav/B001_20260616_1327_c0020.wav`

Le clip WAV est enregistré **pendant l'attente du duty cycle** — il ne retarde pas les mesures.

---

## Horodatage et fuseau horaire

Le RTC PCF8523 stocke **toujours l'heure UTC**. Tous les timestamps CSV et LoRa sont en UTC.

| Situation | Comportement |
|---|---|
| WiFi disponible au démarrage | Sync NTP exacte → heure UTC précise |
| WiFi indisponible | Heure de compilation moins `UTC_OFFSET_SEC` |
| Pile RTC vide | Même chose (idem premier démarrage) |

**`UTC_OFFSET_SEC`** doit correspondre au fuseau horaire de la machine qui compile :
- France heure d'hiver (oct→mars) : `3600` (UTC+1)
- France heure d'été (mars→oct) : `7200` (UTC+2)

---

## WiFi

Utilisé **uniquement au démarrage** pour synchroniser le RTC via NTP, puis déconnecté immédiatement (~40 mA économisés en continu). Si le WiFi est indisponible, le démarrage continue normalement avec l'heure de compilation.

Le **Bluetooth LE** reste actif en permanence pour la configuration terrain et les notifications (nRF Connect compatible).

---

## Beetter Home – Récepteur Python (Raspberry Pi)

### Prérequis

```bash
pip3 install pyserial influxdb-client
```

### Lancement

```bash
python3 receiver.py                    # affichage console
INFLUX_ENABLE=1 INFLUX_TOKEN=xxx python3 receiver.py   # + InfluxDB
```

### Configuration (`receiver.py`)

```python
PORT      = "/dev/serial0"   # GPIO UART — ou "/dev/ttyUSB0" pour USB-UART
FREQUENCE = 868.0            # MHz — doit correspondre à BeetterConfig.h
```

### Champs InfluxDB (nomenclature `influxdb.py` beetter.fr)

| Measurement | Source | Unité |
|---|---|---|
| `temperature_int` / `temperature_ext` | SHT40 | °C |
| `humidity_int` / `humidity_ext` | SHT40 | %RH |
| `sound_freq_int` / `sound_freq_ext` | MFCC – moyenne 11 fenêtres | Hz |
| `sound_amp_int` / `sound_amp_ext` | MFCC – RMS moyen 11 fenêtres | — |
| `light_ext` | GL5539 | indice 0.0–10.0 |
| `mfcc_int_0..12` / `mfcc_ext_0..12` | MFCC – moyennes 11 fenêtres | — |

---

## Optimisations appliquées

| # | Optimisation | Fichier |
|---|---|---|
| 1 | SD : 1 open/close par cycle via `beginCycle/endCycle` | `BeetterSD` |
| 2 | MFCC : fenêtrage 11×2048 + moyennage (cohérence librosa) | `BeetterMic` |
| 3 | Buffer capture statique `_captureBuf[24000]` — zéro malloc | `BeetterMic` |
| 4 | Réinit I2S avant chaque capture (stabilité après WAV) | `BeetterMic` |
| 5 | CSV : `snprintf()` statique — zéro allocation String | `Beetter_Main` |
| 6 | `seqLoRa` uint32_t — overflow à 4 milliards de cycles | `Beetter_Main` |
| 7 | WiFi déconnecté après sync NTP (~40 mA économisés) | `Beetter_Main` |
| 8 | Démarrage autonome — pas de `while(!Serial)` | `Beetter_Main` |
| 9 | UTC à la compilation via `UTC_OFFSET_SEC` | `BeetterRTC` |
| 10 | En-têtes CSV recréés si fichier vide (après effacement SD) | `Beetter_Main` |

---

## GitHub & Démonstrateur

- Code source : https://github.com/Charlox29/Beetter
- Dashboard : https://beetter.fr
