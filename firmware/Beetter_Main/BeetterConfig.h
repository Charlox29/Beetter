/**
 * =============================================================
 *  BeetterConfig.h  –  Configuration centrale du module Beetter Hive
 * =============================================================
 *  Matériel ciblé : ESP32-C6-DevKitC-1-N8
 *  Core Arduino   : arduino-esp32 >= 3.0.0
 * =============================================================
 */

#pragma once

// ─── IDENTIFIANT DE LA RUCHE ─────────────────────────────────
#define BEETTER_HIVE_ID   "B001"

// ─── BROCHES – I2C MATÉRIEL (SHT40 EXT J1 + RTC) ────────────
#define I2C0_SDA   6
#define I2C0_SCL   7

// ─── BROCHES – I2C LOGICIEL (SHT40 INT J3) ───────────────────
#define SW_SDA     10
#define SW_SCL     11

// ─── BROCHES – I2S (microphones ICS-43434 x2) ────────────────
//  J2 (SEL=GND)   → canal GAUCHE  → micro EXTÉRIEUR
//  J5 (SEL=3.3V)  → canal DROIT   → micro INTÉRIEUR
#define I2S_BCLK   2
#define I2S_WS     1
#define I2S_DIN    3

// ─── BROCHES – SPI (micro-SD ADA4682) ────────────────────────
#define SD_SCK     18
#define SD_MISO    20
#define SD_MOSI    19
#define SD_CS      21
#define SD_DET     22

// ─── BROCHES – LoRa (Grove RFM95 via UART1) ──────────────────
#define LORA_RX_PIN  5
#define LORA_TX_PIN  4
#define LORA_FREQ    868.0f

// ─── BROCHES – Photorésistance ───────────────────────────────
#define PHOTO_PIN    0

// ─── PHOTORÉSISTANCE GL5539 – Paramètres physiques ───────────
//  Diviseur de tension : 3.3V ── R_fixe ── GPIO0 ── R_photo ── GND
#define R_FIXE_OHM   10000.0f   // résistance fixe du diviseur (Ω)
#define LDR_GAMMA    0.7f        // exposant GL5539 datasheet (calibré ADC=2642 → 300 lux)
#define LDR_A        297957.0f   // calibré : ADC=2642 (bureau éclairé) = 300 lux
#define LUX_MAX      100000.0f   // référence 10/10 = plein soleil (lux)

// ─── LED RGB intégrée ────────────────────────────────────────
#define LED_RGB_PIN  8           // GPIO8 = WS2812, piloté via neopixelWrite()

// ─── MICROPHONE INTÉRIEUR (canal DROIT – J5, SEL=3.3V) ───────
#define MIC_INT_SAMPLE_RATE_HZ     8000
#define MIC_INT_FREQ_MIN_HZ        50
#define MIC_INT_FREQ_MAX_HZ        2000
#define MIC_INT_CAPTURE_MS         3000   // 3s = 94KB malloc

// ─── MICROPHONE EXTÉRIEUR (canal GAUCHE – J2, SEL=GND) ───────
#define MIC_EXT_SAMPLE_RATE_HZ     8000
#define MIC_EXT_FREQ_MIN_HZ        50
#define MIC_EXT_FREQ_MAX_HZ        2000
#define MIC_EXT_CAPTURE_MS         3000

// ─── MFCC ────────────────────────────────────────────────────
#define MFCC_NUM_COEFFS   13
#define MFCC_NUM_FILTERS  26

// ─── CYCLE DE COLLECTE ───────────────────────────────────────
//  Durée d'un cycle complet (séquentiel) :
//   SHT40      : ~20ms
//   Photo      : ~5ms
//   MFCC INT   : ~3000ms
//   MFCC EXT   : ~3000ms
//   LoRa envoi : ~242ms
//   TOTAL      : ~6.3s actif
//
//  Duty cycle LoRa 868MHz (ETSI) : 1% max
//  ToA mesuré ENV+AUD : ~242ms → intervalle min légal : 25s
//  On utilise 30s pour être à l'aise.
#define CYCLE_INTERVAL_MS   30000UL   // 30s entre deux cycles complets

// ─── STOCKAGE SD ─────────────────────────────────────────────
#define SD_LOG_ENV      "/env.csv"
#define SD_LOG_MFCC_INT "/mfcc_int.csv"
#define SD_LOG_MFCC_EXT "/mfcc_ext.csv"

// ─── WIFI ────────────────────────────────────────────────────
#define WIFI_SSID      "VotreSSID"
#define WIFI_PASSWORD  "VotreMotDePasse"
#define NTP_SERVER     "pool.ntp.org"
#define NTP_GMT_OFFSET_SEC    0      // UTC – le RTC stocke toujours UTC
#define NTP_DST_OFFSET_SEC    0

// ─── BLUETOOTH ───────────────────────────────────────────────
#define BT_DEVICE_NAME   "Beetter-Hive"

// ─── FUSEAU HORAIRE DE COMPILATION ───────────────────────────
// Décalage en secondes entre l'heure locale de la machine qui compile
// et UTC. Utilisé pour corriger reglerDepuisCompilation().
// France heure d'hiver : 3600 (UTC+1)
// France heure d'été   : 7200 (UTC+2)
#define UTC_OFFSET_SEC  3600
