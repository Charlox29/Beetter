/**
 * =============================================================
 *  BeetterConfig.h  –  Configuration centrale du module Beetter Hive
 * =============================================================
 *  Modifiez CE FICHIER UNIQUEMENT pour adapter le comportement du
 *  système : broches, intervalles, paramètres audio, ID de la ruche…
 *
 *  Matériel ciblé : ESP32-C6-DevKitC-1-N8
 *  Core Arduino   : arduino-esp32 >= 3.0.0
 * =============================================================
 */

#pragma once

// ─────────────────────────────────────────────
//  IDENTIFIANT DE LA RUCHE
//  Changer pour chaque Beetter Hive déployé.
//  Format : "BXXX" (3 chiffres max)
// ─────────────────────────────────────────────
#define BEETTER_HIVE_ID   "B001"

// ─────────────────────────────────────────────
//  BROCHES – I2C MATÉRIEL (bus 0)
//  Utilisé par : SHT40 #1 (intérieur) + RTC PCF8523
// ─────────────────────────────────────────────
#define I2C0_SDA   6
#define I2C0_SCL   7

// ─────────────────────────────────────────────
//  BROCHES – I2C LOGICIEL (bit-bang, bus 1)
//  Utilisé par : SHT40 #2 (extérieur)
//  Pull-ups externes 4.7k requis sur ces lignes.
// ─────────────────────────────────────────────
#define SW_SDA     10
#define SW_SCL     11

// ─────────────────────────────────────────────
//  BROCHES – I2S (microphones ICS-43434 x2)
//  J2 (SEL=GND)  → canal GAUCHE  (micro intérieur)
//  J5 (SEL=3.3V) → canal DROIT   (micro extérieur)
// ─────────────────────────────────────────────
#define I2S_BCLK   2
#define I2S_WS     1
#define I2S_DIN    3

// ─────────────────────────────────────────────
//  BROCHES – SPI (lecteur micro-SD ADA4682)
// ─────────────────────────────────────────────
#define SD_SCK     18
#define SD_MISO    20
#define SD_MOSI    19
#define SD_CS      21
#define SD_DET     22   // HIGH = carte présente (pull-up intégré au module)

// ─────────────────────────────────────────────
//  BROCHES – LoRa (Grove RFM95 via UART1)
//  TX module → RX ESP32, RX module → TX ESP32
// ─────────────────────────────────────────────
#define LORA_RX_PIN  5   // relié au TX du module Grove
#define LORA_TX_PIN  4   // relié au RX du module Grove
#define LORA_FREQ    868.0f  // MHz – doit correspondre à Beetter Home

// ─────────────────────────────────────────────
//  BROCHES – Photorésistance (10 kΩ)
// ─────────────────────────────────────────────
#define PHOTO_PIN    0   // GPIO0, entrée analogique ADC

// ─────────────────────────────────────────────
//  CAPTEURS ENVIRONNEMENTAUX – Intervalles (ms)
//  SHT40 intérieur (#1) et extérieur (#2)
//  Photorésistance
// ─────────────────────────────────────────────
#define SHT40_SAMPLE_INTERVAL_MS   10000UL   // lecture toutes les 10 s
#define PHOTO_SAMPLE_INTERVAL_MS   10000UL   // idem

// ─────────────────────────────────────────────
//  MICROPHONE INTÉRIEUR (canal GAUCHE – J2)
//  Plage fréquentielle analysée par FFT/MFCC
// ─────────────────────────────────────────────
#define MIC_INT_SAMPLE_RATE_HZ     8000      // fréquence d'échantillonnage (Hz)
                                              // Rappel Shannon : Fs >= 2 × Fmax
#define MIC_INT_FREQ_MIN_HZ        50        // borne basse d'intérêt (Hz)
#define MIC_INT_FREQ_MAX_HZ        2000       // borne haute d'intérêt (Hz)
#define MIC_INT_CAPTURE_MS         3000      // durée de capture par analyse (ms)
#define MIC_INT_INTERVAL_MS        10000UL   // intervalle entre deux analyses (ms)

// ─────────────────────────────────────────────
//  MICROPHONE EXTÉRIEUR (canal DROIT – J5)
// ─────────────────────────────────────────────
#define MIC_EXT_SAMPLE_RATE_HZ     8000
#define MIC_EXT_FREQ_MIN_HZ        50
#define MIC_EXT_FREQ_MAX_HZ        2000
#define MIC_EXT_CAPTURE_MS         3000
#define MIC_EXT_INTERVAL_MS        10000UL

// ─────────────────────────────────────────────
//  PARAMÈTRES MFCC
//  Nombre de coefficients cepstraux renvoyés
//  (13 est la valeur classique en apiculture acoustique)
// ─────────────────────────────────────────────
#define MFCC_NUM_COEFFS   13
#define MFCC_NUM_FILTERS  26    // banque de filtres Mel

// ─────────────────────────────────────────────
//  TRANSMISSION LoRa – Intervalle d'envoi (ms)
//  Une trame JSON est envoyée à cet intervalle,
//  regroupant les dernières mesures disponibles.
// ─────────────────────────────────────────────
#define LORA_SEND_INTERVAL_MS   20000UL   // 20s minimum legal (ToA reel 176ms, DC 0.88%)

// ─────────────────────────────────────────────
//  STOCKAGE SD – Chemins des fichiers de log
// ─────────────────────────────────────────────
#define SD_LOG_ENV      "/env.csv"     // températures, humidités, luminosité
#define SD_LOG_MFCC_INT "/mfcc_int.csv"
#define SD_LOG_MFCC_EXT "/mfcc_ext.csv"
#define SD_LOG_FFT_INT  "/fft_int.csv"
#define SD_LOG_FFT_EXT  "/fft_ext.csv"

// ─────────────────────────────────────────────
//  WIFI (optionnel – pour sync NTP du RTC)
// ─────────────────────────────────────────────
#define WIFI_SSID      "VotreSSID"
#define WIFI_PASSWORD  "VotreMotDePasse"
#define NTP_SERVER     "pool.ntp.org"
#define NTP_GMT_OFFSET_SEC    3600      // UTC+1 (France heure standard)
#define NTP_DST_OFFSET_SEC    3600      // +1h heure d'été

// ─────────────────────────────────────────────
//  BLUETOOTH
// ─────────────────────────────────────────────
#define BT_DEVICE_NAME   "Beetter-Hive"
