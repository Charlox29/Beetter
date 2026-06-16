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
│       ├── BeetterLoRa.cpp/.h
│       ├── BeetterMic.cpp/.h
│       ├── BeetterPhoto.h
│       ├── BeetterRTC.cpp/.h
│       ├── BeetterSD.cpp/.h
│       ├── BeetterSHT40.cpp/.h
│       └── BeetterWifi.h
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

### 4. Flasher

1. Ouvrir `Beetter_Main/Beetter_Main.ino`
2. Éditer `BeetterConfig.h` (identifiant ruche, WiFi)
3. Compiler et flasher — le programme démarre automatiquement sans moniteur série

---

## Brochage ESP32-C6

| Composant | Broche(s) | Bus |
|---|---|---|
| SHT40 EXTÉRIEUR (J1) | SDA=GPIO6, SCL=GPIO7 | I2C matériel |
| SHT40 INTÉRIEUR (J3) | SDA=GPIO10, SCL=GPIO11 | I2C logiciel |
| RTC PCF8523 | SDA=GPIO6, SCL=GPIO7 | I2C matériel (partagé) |
| Micro EXTÉRIEUR (J2, SEL=GND) | BCLK=GPIO2, WS=GPIO1, DIN=GPIO3 | I2S canal gauche |
| Micro INTÉRIEUR (J5, SEL=3.3V) | Partagé BCLK/WS/DIN | I2S canal droit |
| Micro-SD ADA4682 | SCK=18, MISO=20, MOSI=19, CS=21, DET=22 | SPI |
| LoRa Grove RFM95 | RX=GPIO5, TX=GPIO4 | UART1 @ 57600 |
| Photorésistance GL5539 | GPIO0 | ADC 12 bits |
| LED RGB WS2812 | GPIO8 | neopixelWrite() |

---

## Architecture du firmware

### Cycle séquentiel (toutes les 30s)

```
┌─────────────────────────────────────────────────────────┐
│  Cycle complet (~6.5s actif + ~23.5s attente)           │
│                                                         │
│  1. SHT40 INT + EXT      ~20 ms   T°, %RH, humidité abs│
│  2. Photorésistance        ~5 ms   indice 0.0–10.0      │
│  3. MFCC intérieur      ~3000 ms   canal droit J5       │
│  4. MFCC extérieur      ~3000 ms   canal gauche J2      │
│  5. Écriture SD           ~15 ms   3 fichiers CSV       │
│  6. Envoi LoRa           ~242 ms   ENV+AUD 96 bytes     │
│  7. Attente              ~23s      jusqu'à t=30s        │
└─────────────────────────────────────────────────────────┘
```

Tout ce qui est mesuré est envoyé — pas de mesure inutile.

### LED RGB (GPIO8)

| Couleur | Signification |
|---|---|
| Vert 500ms | Démarrage OK |
| Bleu pendant l'envoi | Transmission LoRa en cours |

---

## MFCC – Analyse acoustique

### Pipeline complet

```
Capture I2S (3s × 8000 Hz = 24 000 samples par micro)
        ↓
  ┌─────────────────────────────────────┐
  │  11 fenêtres × 2048 samples (256ms) │
  │  ┌──────────────────────────────┐   │
  │  │ Hamming → FFT → magnitude   │   │
  │  │ → filtres Mel → log → DCT   │   │
  │  │ → MFCC[13] par fenêtre      │   │
  │  └──────────────────────────────┘   │
  └─────────────────────────────────────┘
        ↓ moyenne des 11 vecteurs
  MFCC[13] représentatif de 3s
```

### Cohérence avec l'entraînement ML

Le fenêtrage temporel + moyennage est **identique au comportement par défaut de `librosa.feature.mfcc()`**, utilisé lors de l'entraînement du modèle. Les vecteurs produits par le firmware sont donc directement comparables aux données d'entraînement.

### Mémoire

| Élément | Taille | Nature |
|---|---|---|
| `_captureBuf[24000]` | 94 KB | Statique — alloué au démarrage, jamais fragmenté |
| `_vReal[2048]` + `_vImag[2048]` | 32 KB | Statique — réutilisés par fenêtre |
| Accumulateurs moyennage | ~120 bytes | Stack — négligeable |

Les 2 micros partagent le même `_captureBuf` séquentiellement :
- t=0s : capture + analyse micro INT → résultats sauvegardés → buffer écrasé
- t=3s : capture + analyse micro EXT → résultats sauvegardés → buffer écrasé

**48 000 samples au total, 94 KB en mémoire.**

### Paramètres configurables (`BeetterConfig.h`)

| Paramètre | Valeur | Description |
|---|---|---|
| `MIC_INT_CAPTURE_MS` | 3000 | Durée capture intérieur (ms) |
| `MIC_EXT_CAPTURE_MS` | 3000 | Durée capture extérieur (ms) |
| `MIC_INT_FREQ_MIN_HZ` | 50 | Borne basse plage d'intérêt |
| `MIC_INT_FREQ_MAX_HZ` | 2000 | Borne haute — couvre H1–H8 abeille |
| `MFCC_NUM_COEFFS` | 13 | Coefficients cepstraux |
| `MFCC_NUM_FILTERS` | 26 | Filtres Mel (standard apicole) |

Signatures acoustiques couvertes (50–2000 Hz) :

| Comportement | Fréquence |
|---|---|
| Colonie calme | 200–250 Hz |
| Ventilation | 200–230 Hz |
| Stress léger | 300–400 Hz |
| Essaimage imminent | 350–500 Hz |
| Orphelinage | 300–450 Hz |
| Intrusion frelon | 400–600 Hz |
| Harmoniques H2–H8 | 460–1840 Hz |

---

## Photorésistance GL5539

Modèle physique calibré sur l'exemplaire B001 :

```
ADC (0..4095) → R_ldr → Lux → indice log 0.0–10.0
```

| Paramètre | Valeur | Description |
|---|---|---|
| `R_FIXE_OHM` | 10 000 Ω | Résistance du diviseur |
| `LDR_GAMMA` | 0.7 | Exposant GL5539 datasheet |
| `LDR_A` | 297 957 | Calibré : ADC=2642 (bureau éclairé) = 300 lux |
| `LUX_MAX` | 100 000 lux | Référence 10/10 (plein soleil) |

L'indice est logarithmique (échelle Lux) pour refléter la perception humaine de la luminosité. `0.0` = nuit complète, `10.0` = plein soleil direct.

---

## Protocole LoRa – Blocs binaires

Une trame = 2 blocs concaténés = **96 bytes**, envoyés toutes les 30s.

### BLOC ENV – `0xE0` – 23 bytes

| Offset | Bytes | Champ | Encodage |
|---|---|---|---|
| 0 | 1 | magic | `0xE0` |
| 1 | 2 | seq | uint32 côté firmware, tronqué uint16 dans trame |
| 3 | 4 | hive_id | `"B001"` |
| 7 | 4 | ts | uint32 Unix UTC |
| 11 | 2 | temp_int | int16 = °C × 100 |
| 13 | 2 | hum_int | uint16 = %RH × 10 |
| 15 | 2 | temp_ext | int16 = °C × 100 |
| 17 | 2 | hum_ext | uint16 = %RH × 10 |
| 19 | 2 | light_ext | uint16 = indice × 10 |
| 21 | 2 | crc16 | CRC-16/CCITT |

### BLOC AUD – `0xA0` – 73 bytes

| Offset | Bytes | Champ | Encodage |
|---|---|---|---|
| 0 | 1 | magic | `0xA0` |
| 1 | 2 | seq | uint16 |
| 3 | 4 | hive_id | `"B001"` |
| 7 | 4 | ts | uint32 Unix UTC |
| 11 | 2 | freq_int | uint16 = Hz × 10 |
| 13 | 2 | rms_int | uint16 = × 10000 |
| 15 | 2 | freq_ext | uint16 = Hz × 10 |
| 17 | 2 | rms_ext | uint16 = × 10000 |
| 19 | 26 | mfcc_int[13] | int16[13] = × 100 |
| 45 | 26 | mfcc_ext[13] | int16[13] = × 100 |
| 71 | 2 | crc16 | CRC-16/CCITT |

### Duty cycle ETSI 868 MHz (1%)

| Mode | Taille | ToA mesuré | DC@30s |
|---|---|---|---|
| ENV+AUD | 96 bytes | ~176 ms | 0.59% ✅ |

---

## Micro-SD – Fichiers CSV

| Fichier | En-tête |
|---|---|
| `/env.csv` | `timestamp, T_int_C, H_int_RH, Ha_int_gm3, T_ext_C, H_ext_RH, Ha_ext_gm3, lum_brut, lum_indice` |
| `/mfcc_int.csv` | `timestamp, freq_dom_Hz, rms, mfcc0..mfcc12` |
| `/mfcc_ext.csv` | `timestamp, freq_dom_Hz, rms, mfcc0..mfcc12` |

Les 3 fichiers sont ouverts en une seule opération par cycle (`beginCycle/endCycle`) pour minimiser l'usure du système FAT.

L'en-tête CSV est réécrit automatiquement si le fichier est **absent ou vide** — vider le contenu d'un fichier CSV puis redémarrer l'ESP32 suffit pour réinitialiser proprement.

---

## Beetter Home – Récepteur Python (Raspberry Pi)

### Prérequis

```bash
pip3 install pyserial influxdb-client
```

### Lancement

```bash
# Affichage console uniquement
python3 receiver.py

# Avec écriture dans InfluxDB
INFLUX_ENABLE=1 INFLUX_TOKEN=xxx python3 receiver.py
```

### Configuration (`receiver.py`)

```python
PORT      = "/dev/serial0"   # GPIO UART Pi — ou "/dev/ttyUSB0" pour USB-UART
FREQUENCE = 868.0            # MHz — doit correspondre à BeetterConfig.h
```

### Champs InfluxDB (nomenclature `influxdb.py` beetter.fr)

| Measurement | Source | Unité |
|---|---|---|
| `temperature_int` / `temperature_ext` | SHT40 | °C |
| `humidity_int` / `humidity_ext` | SHT40 | %RH |
| `sound_freq_int` / `sound_freq_ext` | MFCC | Hz |
| `sound_amp_int` / `sound_amp_ext` | MFCC | — |
| `light_ext` | GL5539 | indice 0–10 |
| `mfcc_int_0..12` / `mfcc_ext_0..12` | MFCC | — |

---

## Optimisations appliquées

| # | Optimisation | Fichier |
|---|---|---|
| 1 | SD : 1 open/close par cycle via `beginCycle/endCycle` | `BeetterSD` |
| 2 | MFCC : fenêtrage temporel 11×2048 + moyennage (cohérence ML) | `BeetterMic` |
| 3 | Buffer capture statique `_captureBuf[24000]` — pas de malloc | `BeetterMic` |
| 4 | CSV : `snprintf()` statique — pas de `String` dynamique | `Beetter_Main` |
| 5 | `seqLoRa` uint32_t — pas d'overflow à 65 535 cycles | `Beetter_Main` |
| 6 | WiFi déconnecté après sync NTP (~40 mA économisés) | `Beetter_Main` |
| 7 | Démarrage autonome — pas de `while(!Serial)` | `Beetter_Main` |
| 8 | Compilation UTC — `UTC_OFFSET_SEC` soustrait à `__TIME__` | `BeetterRTC` |

---

## GitHub & Démonstrateur

- Code source : https://github.com/Charlox29/Beetter
- Dashboard : https://beetter.fr
