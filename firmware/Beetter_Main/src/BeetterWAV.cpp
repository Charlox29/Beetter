/**
 * BeetterWAV.cpp  –  Enregistrement WAV stéréo sur micro-SD
 */

#include "BeetterWAV.h"
#include "../BeetterConfig.h"

void BeetterWAV::begin(uint8_t bclk, uint8_t ws, uint8_t din,
                        uint32_t sampleRate) {
    _bclk       = bclk;
    _ws         = ws;
    _din        = din;
    _sampleRate = sampleRate;
    _nbClips    = 0;
    _creerDossier();
    Serial.printf("[WAV] Init – %ds tous les %d cycles (/wav/)\n",
                  WAV_DUREE_SEC, WAV_EVERY_CYCLES);
}

void BeetterWAV::_creerDossier() {
    if (!SD.exists("/wav")) SD.mkdir("/wav");
}

void BeetterWAV::enregistrerSiNecessaire(uint32_t seqCycle, BeetterRTC& rtc) {
    if (seqCycle == 0 || (seqCycle % WAV_EVERY_CYCLES) != 0) return;

    char chemin[64];
    if (rtc.isReady()) {
        // Nom basé sur l'horodatage complet (secondes incluses)
        // → pas de doublon même après un redémarrage au même cycle
        // Format : /wav/B001_20260616_132700.wav
        DateTime now = rtc.maintenant();
        snprintf(chemin, sizeof(chemin),
                 "/wav/%s_%04d%02d%02d_%02d%02d%02d.wav",
                 BEETTER_HIVE_ID,
                 now.year(), now.month(), now.day(),
                 now.hour(), now.minute(), now.second());
    } else {
        // Fallback sans RTC : utiliser millis() comme discriminant
        snprintf(chemin, sizeof(chemin),
                 "/wav/%s_%lu.wav",
                 BEETTER_HIVE_ID, (unsigned long)millis());
    }

    Serial.printf("[WAV] Enregistrement → %s (%ds)\n", chemin, WAV_DUREE_SEC);
    _enregistrer(chemin, (uint32_t)WAV_DUREE_SEC * 1000UL);
}

void BeetterWAV::_enregistrer(const char* chemin, uint32_t dureeMs) {
    // ── Init I2S ──
    I2SClass i2s;
    i2s.setPins(_bclk, _ws, -1, _din, -1);
    if (!i2s.begin(I2S_MODE_STD, (int)_sampleRate,
                   I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO)) {
        Serial.println(F("[WAV] Echec init I2S."));
        return;
    }

    // ── Ouvrir le fichier ──
    File f = SD.open(chemin, FILE_WRITE);
    if (!f) {
        Serial.printf("[WAV] Impossible de créer %s\n", chemin);
        i2s.end();
        return;
    }

    // ── En-tête WAV ──
    WavHeader hdr;
    hdr.sampleRate    = _sampleRate;
    hdr.numChannels   = 2;
    hdr.bitsPerSample = 16;
    hdr.blockAlign    = 4;
    hdr.byteRate      = _sampleRate * 4;
    f.write((uint8_t*)&hdr, sizeof(hdr));

    // ── Capture I2S → SD ──
    const uint32_t BLOC = 256;
    int32_t brut[BLOC * 2];
    int16_t pcm [BLOC * 2];
    uint32_t dataOctets = 0;
    unsigned long debut = millis();

    while ((millis() - debut) < dureeMs) {
        size_t lus = i2s.readBytes((char*)brut, BLOC * 2 * sizeof(int32_t));
        uint32_t paires = lus / (2 * sizeof(int32_t));
        for (uint32_t i = 0; i < paires; i++) {
            // ICS-43434 : 24 bits cadrés à gauche → >>8 + atténuation WAV_GAIN_SHIFT
            pcm[i * 2]     = (int16_t)(brut[i * 2]     >> (8 + WAV_GAIN_SHIFT)); // L = EXT
            pcm[i * 2 + 1] = (int16_t)(brut[i * 2 + 1] >> (8 + WAV_GAIN_SHIFT)); // R = INT
        }
        f.write((uint8_t*)pcm, paires * 2 * sizeof(int16_t));
        dataOctets += paires * 2 * sizeof(int16_t);
    }

    i2s.end();
    delay(200);  // Stabilisation bus I2S

    // ── Mise à jour en-tête ──
    hdr.dataSize  = dataOctets;
    hdr.chunkSize = 36 + dataOctets;
    f.seek(0);
    f.write((uint8_t*)&hdr, sizeof(hdr));
    f.close();

    _nbClips++;
    Serial.printf("[WAV] OK – clip #%lu – %.1f MB\n",
                  (unsigned long)_nbClips,
                  (float)(sizeof(hdr) + dataOctets) / 1048576.0f);
}
