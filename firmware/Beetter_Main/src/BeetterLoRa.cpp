/**
 * BeetterLoRa.cpp – Envoi blocs ENV (0xE0) + AUD (0xA0)
 */

#include "BeetterLoRa.h"
#include "../BeetterConfig.h"

// LED_RGB_PIN défini dans BeetterConfig.h (GPIO8 = WS2812)

HardwareSerial          BeetterLoRa::_ss(1);
RH_RF95<HardwareSerial> BeetterLoRa::_radio(BeetterLoRa::_ss);

bool BeetterLoRa::begin(uint8_t rxPin, uint8_t txPin, float frequence) {
    _pret = false; _freq = frequence;
    _tsDernierEnvoi = 0; _tmsEmission = 0; _tsDebutHeure = millis();

    Serial.println(F("[LoRa] Initialisation module Grove RFM95..."));
    _ss.begin(57600, SERIAL_8N1, rxPin, txPin);

    if (!_radio.init()) {
        Serial.println(F("[LoRa] Echec init."));
        return false;
    }
    _radio.setFrequency(frequence);
    Serial.printf("[LoRa] Pret sur %.1f MHz\n", frequence);
    _pret = true;
    return true;
}

bool     BeetterLoRa::isReady()   const { return _pret; }
int      BeetterLoRa::rssiDernier()     { return _radio.lastRssi(); }

uint32_t BeetterLoRa::tempsAvantProchainEnvoi() const {
    if (_tsDernierEnvoi == 0) return 0;
    unsigned long e = millis() - _tsDernierEnvoi;
    return (e >= LORA_INTERVALLE_MIN_MS) ? 0 : (uint32_t)(LORA_INTERVALLE_MIN_MS - e);
}

float BeetterLoRa::dutyCycleUtilise() const {
    unsigned long f = min((unsigned long)(millis() - _tsDebutHeure), 3600000UL);
    return (f == 0) ? 0.0f : (_tmsEmission * 100.0f) / f;
}

void BeetterLoRa::_comptabiliser(uint32_t dureeMs) {
    if (millis() - _tsDebutHeure >= 3600000UL) {
        _tmsEmission = 0; _tsDebutHeure = millis();
    }
    _tmsEmission += dureeMs;
}

uint16_t BeetterLoRa::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

bool BeetterLoRa::_envoyer(const uint8_t* buf, uint8_t len, const char* label) {
    uint32_t attente = tempsAvantProchainEnvoi();
    if (attente > 0) {
        Serial.printf("[LoRa] DC – %s refusé, attendre %lu s\n", label, attente/1000);
        return false;
    }
    Serial.printf("[LoRa] %s (%d bytes)\n", label, len);
    unsigned long debut = millis();

    // ── LED bleue ON pendant l'émission ──
    neopixelWrite(LED_RGB_PIN, 0, 0, 50);  // R=0, G=0, B=50

    bool ok = _radio.send(const_cast<uint8_t*>(buf), len);
    if (ok) {
        _radio.waitPacketSent();
        _comptabiliser((uint32_t)(millis() - debut));
        _tsDernierEnvoi = millis();
        Serial.printf("[LoRa] OK – DC: %.3f%%\n", dutyCycleUtilise());
    }

    // ── LED bleue OFF ──
    neopixelWrite(LED_RGB_PIN, 0, 0, 0);

    return ok;
}

bool BeetterLoRa::envoyerTrame(const char* hiveId, uint32_t ts, uint32_t seq,
                                 float tempInt, float humInt,
                                 float tempExt, float humExt,
                                 float lightIndice,
                                 float freqInt, float rmsInt,
                                 const float* mfccInt, uint8_t nbMfccInt,
                                 float freqExt, float rmsExt,
                                 const float* mfccExt, uint8_t nbMfccExt) {
    if (!_pret) return false;

    // ── Bloc ENV ──
    BlocENV env;
    env.magic    = MAGIC_ENV;
    env.seq      = (uint16_t)(seq & 0xFFFF);  // tronqué proprement pour la trame
    memset(env.hiveId, 0, 4); strncpy(env.hiveId, hiveId, 4);
    env.timestamp = ts;
    env.tempInt   = (int16_t)(tempInt * 100);
    env.humInt    = (uint16_t)(humInt  * 10);
    env.tempExt   = (int16_t)(tempExt * 100);
    env.humExt    = (uint16_t)(humExt  * 10);
    env.lightExt  = (uint16_t)(lightIndice * 10);
    env.crc16     = crc16((uint8_t*)&env, SIZE_ENV - 2);

    // ── Bloc AUD ──
    BlocAUD aud;
    aud.magic    = MAGIC_AUD;
    aud.seq      = (uint16_t)(seq & 0xFFFF);
    memset(aud.hiveId, 0, 4); strncpy(aud.hiveId, hiveId, 4);
    aud.timestamp = ts;
    aud.freqInt   = (uint16_t)(freqInt * 10);
    aud.rmsInt    = (uint16_t)(rmsInt  * 10000);
    aud.freqExt   = (uint16_t)(freqExt * 10);
    aud.rmsExt    = (uint16_t)(rmsExt  * 10000);
    for (int i = 0; i < 13; i++) {
        aud.mfccInt[i] = (i < nbMfccInt) ? (int16_t)(mfccInt[i] * 100) : 0;
        aud.mfccExt[i] = (i < nbMfccExt) ? (int16_t)(mfccExt[i] * 100) : 0;
    }
    aud.crc16 = crc16((uint8_t*)&aud, SIZE_AUD - 2);

    // ── Concaténer ENV + AUD → 1 trame de 96 bytes ──
    uint8_t buf[SIZE_ENV + SIZE_AUD];
    memcpy(buf,           &env, SIZE_ENV);
    memcpy(buf + SIZE_ENV, &aud, SIZE_AUD);

    return _envoyer(buf, SIZE_ENV + SIZE_AUD, "ENV+AUD");
}
