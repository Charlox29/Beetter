/**
 * =============================================================
 *  BeetterWAV.h  –  Enregistrement audio WAV sur micro-SD
 * =============================================================
 *  Enregistre un clip WAV PCM 16 bits stéréo (INT + EXT)
 *  tous les WAV_EVERY_CYCLES cycles, pendant l'attente du duty cycle.
 *
 *  Paramètres (BeetterConfig.h) :
 *   WAV_EVERY_CYCLES  20    → 1 clip toutes les 10 min (20 × 30s)
 *   WAV_DUREE_SEC     15    → durée du clip en secondes
 *   WAV_GAIN_SHIFT     3    → atténuation (-18 dB, adapté au bruit de ruche)
 *
 *  Format fichier : /wav/B001_20260616_1258_c0050.wav
 *   Canal gauche (L) = micro EXTÉRIEUR (J2)
 *   Canal droit  (R) = micro INTÉRIEUR (J5)
 *
 *  Intégration :
 *   BeetterWAV wav;
 *   wav.begin(bclk, ws, din, sampleRate);
 *   wav.enregistrerSiNecessaire(seqLoRa, rtc);
 * =============================================================
 */

#pragma once
#include <Arduino.h>
#include <SD.h>
#include <ESP_I2S.h>
#include "BeetterRTC.h"

#pragma pack(push, 1)
struct WavHeader {
    char     riff[4]       = {'R','I','F','F'};
    uint32_t chunkSize     = 0;
    char     wave[4]       = {'W','A','V','E'};
    char     fmt[4]        = {'f','m','t',' '};
    uint32_t fmtSize       = 16;
    uint16_t audioFormat   = 1;
    uint16_t numChannels   = 2;
    uint32_t sampleRate    = 8000;
    uint32_t byteRate      = 0;
    uint16_t blockAlign    = 0;
    uint16_t bitsPerSample = 16;
    char     data[4]       = {'d','a','t','a'};
    uint32_t dataSize      = 0;
};
#pragma pack(pop)

static_assert(sizeof(WavHeader) == 44, "WavHeader doit faire 44 bytes");

class BeetterWAV {
public:
    void begin(uint8_t bclk, uint8_t ws, uint8_t din,
               uint32_t sampleRate = 8000);

    void enregistrerSiNecessaire(uint32_t seqCycle, BeetterRTC& rtc);

    uint32_t nbClips() const { return _nbClips; }

private:
    uint8_t  _bclk = 0, _ws = 0, _din = 0;
    uint32_t _sampleRate = 8000;
    uint32_t _nbClips    = 0;

    void _enregistrer(const char* chemin, uint32_t dureeMs);
    void _creerDossier();
};
