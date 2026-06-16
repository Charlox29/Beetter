# Beetter Hive – Firmware Arduino

Firmware complet pour le module **Beetter Hive** (ESP32-C6-DevKitC-1-N8).

---

## Structure du projet

```
Beetter/
├── README.md
│
└── Beetter_Main/                ← Programme principal
    ├── Beetter_Main.ino         ← Ouvrir dans Arduino IDE
    ├── BeetterConfig.h          ← Fichier à éditer en premier
    │
    └── lib/
        ├── BeetterSD.cpp/h      → Carte micro-SD (ADA4682)
        ├── BeetterSHT40.cpp/h   → Capteurs T°/Humidité (SHT40 x2)
        ├── BeetterMic.cpp/h     → Microphones I2S + FFT + MFCC (ICS-43434 x2)
        ├── BeetterRTC.cpp/h     → RTC PCF8523 (ADA3295)
        ├── BeetterLoRa.cpp/h    → Transmission LoRa (Grove RFM95)
        ├── BeetterPhoto.h       → Photorésistance (GPIO0)
        └── BeetterWifi.h        → WiFi + Bluetooth LE (ESP32-C6)
```

---

## Installation

### 1. Installer les bibliothèques dans Arduino IDE

**Via le Library Manager (Outils → Gérer les bibliothèques) :**

| Bibliothèque | Version minimale |
|---|---|
| `Adafruit SHT4x` | 1.0.0 |
| `Adafruit BusIO` | 1.14.0 |
| `RTClib` (by Adafruit) | 2.0.0 |
| `arduinoFFT` (by Enrique Condes) | **2.0.0 obligatoire** |

**Via ZIP (GitHub) :**

```
Grove LoRa 433MHz / 868MHz RF :
https://github.com/Seeed-Studio/Grove_LoRa_433MHz_and_915MHz_RF
→ Code → Download ZIP
→ Arduino IDE : Croquis → Inclure une bibliothèque → Ajouter la bibliothèque .ZIP
```

⚠️ Si vous avez déjà installé `RadioHead`, désinstallez-la ou renommez-la avant d'installer la lib Seeed (conflit sur `RH_RF95.h`).

### 2. Installer les bibliothèques locales Beetter

Les bibliothèques Beetter sont incluses localement dans le dossier `Beetter_Main/lib/`. Arduino IDE les détectera automatiquement lors de l'ouverture du projet.

Aucune action supplémentaire n'est requise pour les bibliothèques locales ✓

### 3. Configurer la carte dans Arduino IDE

- **Carte** : `ESP32C6 Dev Module`
- **Core** : arduino-esp32 >= 3.0.0

  URL boards manager : `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

**Paramètres obligatoires à configurer :**
- **Flash Size** : `8MB (64Mb)`
- **Partition Scheme** : `8M with spiffs (3MB APP/1.5MB SPIFFS)`
- **USB CDC On Boot** : `Enabled` (pour la communication série via USB)

### 4. Ouvrir et configurer le projet

1. Ouvrir `Beetter_Main/Beetter_Main.ino` dans Arduino IDE
2. Éditer `Beetter_Main/BeetterConfig.h` (même dossier) :
   - Changer `BEETTER_HIVE_ID` → ex. `"B002"` pour la 2e ruche
   - Renseigner `WIFI_SSID` et `WIFI_PASSWORD`
   - Ajuster les intervalles si besoin

---

## Brochage

| Composant | Broche(s) |
|---|---|
| SHT40 #1 (intérieur) | SDA=GPIO6, SCL=GPIO7 (I2C0) |
| SHT40 #2 (extérieur) | SDA=GPIO10, SCL=GPIO11 (soft) |
| RTC PCF8523 | SDA=GPIO6, SCL=GPIO7 (I2C0) |
| Micro INT (gauche, J2) | BCLK=GPIO2, WS=GPIO1, DIN=GPIO3 |
| Micro EXT (droit, J5) | Partagé – SEL=3.3V |
| Micro-SD | SCK=18, MISO=20, MOSI=19, CS=21, DET=22 |
| LoRa Grove | RX=GPIO4, TX=GPIO5 (UART1) |
| Photorésistance | GPIO0 (ADC) |

---

## Format des trames LoRa (JSON)

```json
{
  "id":   "B001",
  "ts":   1718000000,
  "Ti":   35.20,  "Hi": 58.1,  "Ai": 12.30,
  "Te":   22.10,  "He": 65.0,  "Ae":  9.80,
  "L":    1823,
  "Fd_i": 250.3,  "Er_i": 0.1200,
  "Fd_e": 230.1,  "Er_e": 0.0800,
  "mc_i": [1.200, -0.300, 0.500, ...],
  "mc_e": [0.800, -0.100, 0.200, ...]
}
```

| Champ | Description |
|---|---|
| `id` | ID de la ruche |
| `ts` | Timestamp Unix |
| `Ti/Te` | Température int/ext (°C) |
| `Hi/He` | Humidité relative int/ext (%RH) |
| `Ai/Ae` | Humidité absolue int/ext (g/m³) |
| `L` | Luminosité ADC brut (0..4095) |
| `Fd_i/e` | Fréquence dominante micro int/ext (Hz) |
| `Er_i/e` | Énergie RMS micro int/ext (normalisée) |
| `mc_i/e` | Coefficients MFCC micro int/ext |

---

## Fichiers CSV sur la micro-SD

| Fichier | Contenu |
|---|---|
| `/env.csv` | T°, humidité, luminosité horodatés |
| `/mfcc_int.csv` | Fréq. dominante + MFCC micro intérieur |
| `/mfcc_ext.csv` | Fréq. dominante + MFCC micro extérieur |

---

## Bluetooth LE

L'ESP32-C6 annonce un serveur BLE nommé `Beetter-Hive` avec :
- **Service UUID** : `12345678-1234-1234-1234-123456789abc`
- **Caractéristique** : `abcdef01-1234-1234-1234-123456789abc` (READ/WRITE/NOTIFY)

À chaque envoi LoRa, la même trame JSON est notifiée aux clients BLE connectés.
Utile pour la mise au point terrain avec une application comme **nRF Connect** (iOS/Android).

---

## Paramètres audio recommandés

| Paramètre | Valeur | Justification |
|---|---|---|
| Fréquence d'échantillonnage | 8000 Hz | Suffisant pour [50–600 Hz] (Shannon × 13) |
| Durée de capture | 3000 ms | ~3 s de bourdon = analyse spectrale fiable |
| Intervalle d'analyse | 10000 ms | Toutes les 10 s |
| Fréquences cibles | 50–600 Hz | Zone de signature apicole (Tableau 7 du rapport) |
| Coefficients MFCC | 13 | Standard reconnaissance acoustique |
| Taille FFT | 2048 | Résolution fréquentielle : 3.9 Hz à 8 kHz |

---

## GitHub & Documentation

- Dépôt source : https://github.com/Charlox29/Beetter
- Démonstrateur : https://beetter.fr
