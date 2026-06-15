/**
 * =============================================================
 *  BeetterLoRa.cpp  –  Trame binaire fixe 83 bytes
 * =============================================================
 */

#include "BeetterLoRa.h"

HardwareSerial          BeetterLoRa::_ss(1);
RH_RF95<HardwareSerial> BeetterLoRa::_radio(BeetterLoRa::_ss);

// ─── Initialisation ──────────────────────────────────────────
bool BeetterLoRa::begin(uint8_t rxPin, uint8_t txPin, float frequence) {
    _pret             = false;
    _freq             = frequence;
    _tsDernierEnvoi   = 0;
    _tmsEmissionHeure = 0;
    _tsDebutHeure     = millis();

    Serial.println(F("[LoRa] Initialisation du module Grove RFM95..."));
    _ss.begin(57600, SERIAL_8N1, rxPin, txPin);

    if (!_radio.init()) {
        Serial.println(F("[LoRa] Echec init. Verifier cablage RX/TX."));
        return false;
    }

    _radio.setFrequency(frequence);
    Serial.printf("[LoRa] Pret sur %.1f MHz – trame binaire %d bytes\n",
                  frequence, BEETTER_FRAME_SIZE);
    Serial.printf("[LoRa] Duty cycle 1%% – intervalle min legal : %lu ms\n",
                  LORA_INTERVALLE_MIN_MS);
    _pret = true;
    return true;
}

bool     BeetterLoRa::isReady()   const { return _pret; }
int      BeetterLoRa::rssiDernier()     { return _radio.lastRssi(); }

// ─── Duty cycle ──────────────────────────────────────────────
uint32_t BeetterLoRa::tempsAvantProchainEnvoi() const {
    if (_tsDernierEnvoi == 0) return 0;
    unsigned long ecoule = millis() - _tsDernierEnvoi;
    if (ecoule >= LORA_INTERVALLE_MIN_MS) return 0;
    return (uint32_t)(LORA_INTERVALLE_MIN_MS - ecoule);
}

float BeetterLoRa::dutyCycleUtilise() const {
    unsigned long fenetreMs = min((unsigned long)(millis() - _tsDebutHeure),
                                  3600000UL);
    if (fenetreMs == 0) return 0.0f;
    return (_tmsEmissionHeure * 100.0f) / fenetreMs;
}

void BeetterLoRa::_comptabiliserEmission(uint32_t dureeMs) {
    if (millis() - _tsDebutHeure >= 3600000UL) {
        _tmsEmissionHeure = 0;
        _tsDebutHeure     = millis();
    }
    _tmsEmissionHeure += dureeMs;
}

// ─── CRC-16/CCITT (poly 0x1021, init 0xFFFF) ─────────────────
uint16_t BeetterLoRa::calculerCRC16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

// ─── Envoi ───────────────────────────────────────────────────
bool BeetterLoRa::envoyerTrame(TrameBeetter& trame) {
    if (!_pret) return false;

    // ── Vérification duty cycle ──
    uint32_t attente = tempsAvantProchainEnvoi();
    if (attente > 0) {
        Serial.printf("[LoRa] DUTY CYCLE – envoi refuse. "
                      "Attendre %lu s (DC: %.3f%%)\n",
                      attente / 1000, dutyCycleUtilise());
        return false;
    }

    // ── Calcul et insertion du CRC sur les 81 premiers bytes ──
    trame.crc16 = calculerCRC16((uint8_t*)&trame,
                                  BEETTER_FRAME_SIZE - sizeof(uint16_t));

    // ── Envoi binaire brut ──
    Serial.printf("[LoRa] Envoi trame #%u (%d bytes)\n",
                  trame.seq, BEETTER_FRAME_SIZE);

    unsigned long debut = millis();
    bool ok = _radio.send((uint8_t*)&trame, BEETTER_FRAME_SIZE);
    if (ok) {
        _radio.waitPacketSent();
        uint32_t toa = (uint32_t)(millis() - debut);
        _comptabiliserEmission(toa);
        _tsDernierEnvoi = millis();
        Serial.printf("[LoRa] OK – ToA: %lu ms – DC: %.3f%%\n",
                      toa, dutyCycleUtilise());
    } else {
        Serial.println(F("[LoRa] Echec envoi."));
    }

    return ok;
}

// ─── Réception ───────────────────────────────────────────────
bool BeetterLoRa::recevoirMessage(uint8_t* buf, uint8_t& len,
                                   uint32_t timeoutMs) {
    if (!_pret) return false;
    unsigned long debut = millis();
    len = RH_RF95_MAX_MESSAGE_LEN;
    while ((millis() - debut) < timeoutMs) {
        if (_radio.available() && _radio.recv(buf, &len)) return true;
        delay(10);
    }
    return false;
}

// ─── Affichage debug ─────────────────────────────────────────
void BeetterLoRa::afficherTrame(const TrameBeetter& t) {
    Serial.println(F("[LoRa] ── Contenu trame ──────────────────"));
    Serial.printf("  magic     : 0x%02X\n", t.magic);
    Serial.printf("  seq       : %u\n",     t.seq);
    Serial.printf("  hive_id   : %.4s\n",   t.hiveId);
    Serial.printf("  timestamp : %lu\n",    (unsigned long)t.timestamp);
    Serial.printf("  temp_int  : %.2f °C\n",  t.tempInt  / 100.0f);
    Serial.printf("  hum_int   : %.1f %%RH\n", t.humInt  / 10.0f);
    Serial.printf("  temp_ext  : %.2f °C\n",  t.tempExt  / 100.0f);
    Serial.printf("  hum_ext   : %.1f %%RH\n", t.humExt  / 10.0f);
    Serial.printf("  lum       : %u ADC\n",   t.luminosite);
    Serial.printf("  freq_int  : %.1f Hz\n",  t.freqDomInt  / 10.0f);
    Serial.printf("  rms_int   : %.4f\n",     t.energieRMSInt / 10000.0f);
    Serial.printf("  freq_ext  : %.1f Hz\n",  t.freqDomExt  / 10.0f);
    Serial.printf("  rms_ext   : %.4f\n",     t.energieRMSExt / 10000.0f);

    Serial.print(F("  mfcc_int  : ["));
    for (int i = 0; i < 13; i++)
        Serial.printf(i < 12 ? "%.2f," : "%.2f", t.mfccInt[i] / 100.0f);
    Serial.println(']');

    Serial.print(F("  mfcc_ext  : ["));
    for (int i = 0; i < 13; i++)
        Serial.printf(i < 12 ? "%.2f," : "%.2f", t.mfccExt[i] / 100.0f);
    Serial.println(']');

    Serial.printf("  crc16     : 0x%04X\n", t.crc16);

    // Bytes hex bruts
    Serial.print(F("  hex       : "));
    const uint8_t* raw = (const uint8_t*)&t;
    for (int i = 0; i < BEETTER_FRAME_SIZE; i++)
        Serial.printf("%02X ", raw[i]);
    Serial.println();
    Serial.println(F("[LoRa] ──────────────────────────────────"));
}
