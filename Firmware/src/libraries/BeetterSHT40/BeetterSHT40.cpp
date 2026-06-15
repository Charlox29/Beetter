/**
 * =============================================================
 *  BeetterSHT40.cpp  –  Implémentation capteurs T°/Humidité
 *
 *  Mapping physique corrigé :
 *   J1 (bus matériel  GPIO6/7)   = capteur EXTÉRIEUR
 *   J3 (bus logiciel  GPIO10/11) = capteur INTÉRIEUR
 * =============================================================
 */

#include "BeetterSHT40.h"
#include <math.h>

bool BeetterSHT40::begin(uint8_t sda0, uint8_t scl0,
                          uint8_t sda1, uint8_t scl1) {
    _swSDA = sda1;
    _swSCL = scl1;

    // ── Capteur EXTÉRIEUR : bus I2C matériel (J1) ──
    Wire.begin(sda0, scl0);
    _ok_ext = _sht_ext.begin(&Wire);
    if (_ok_ext) {
        _sht_ext.setPrecision(SHT4X_HIGH_PRECISION);
        _sht_ext.setHeater(SHT4X_NO_HEATER);
        Serial.println(F("[SHT40] Capteur EXTERIEUR (J1, bus materiel) OK."));
    } else {
        Serial.println(F("[SHT40] Capteur EXTERIEUR (J1) non trouve."));
    }

    // ── Capteur INTÉRIEUR : bus I2C logiciel (J3) ──
    _sdaHigh(); _sclHigh();
    delayMicroseconds(10);
    _swStart();
    _ok_int = _swWrite((SHT40_ADDR << 1) | 0);
    _swStop();

    if (_ok_int) {
        Serial.println(F("[SHT40] Capteur INTERIEUR (J3, bus logiciel) OK."));
    } else {
        Serial.println(F("[SHT40] Capteur INTERIEUR (J3) non trouve. Verifier GPIO10/11 + pull-ups."));
    }

    _derniereInt = {0, 0, 0, false};
    _derniereExt = {0, 0, 0, false};

    return _ok_ext || _ok_int;
}

bool BeetterSHT40::interieurPret() const { return _ok_int; }
bool BeetterSHT40::exterieurPret() const { return _ok_ext; }

// ─── Lecture INTÉRIEUR (bus logiciel – J3) ───────────────────
MesureSHT40 BeetterSHT40::lireInterieur(uint32_t intervalle_ms) {
    if (!_ok_int) return {0, 0, 0, false};

    unsigned long maintenant = millis();
    if (_tsDerniereInt != 0 &&
        (maintenant - _tsDerniereInt) < intervalle_ms) {
        return _derniereInt;
    }

    float tC = 0, rh = 0;
    bool ok = _lireRaw_int(tC, rh);

    _derniereInt.temperatureC = tC;
    _derniereInt.humiditeRH   = rh;
    _derniereInt.humiditeAbs  = ok ? humiditeAbsolue(tC, rh) : 0;
    _derniereInt.valide       = ok;
    if (ok) _tsDerniereInt = maintenant;

    return _derniereInt;
}

// ─── Lecture EXTÉRIEUR (bus matériel – J1) ───────────────────
MesureSHT40 BeetterSHT40::lireExterieur(uint32_t intervalle_ms) {
    if (!_ok_ext) return {0, 0, 0, false};

    unsigned long maintenant = millis();
    if (_tsDerniereExt != 0 &&
        (maintenant - _tsDerniereExt) < intervalle_ms) {
        return _derniereExt;
    }

    sensors_event_t hum, temp;
    _sht_ext.getEvent(&hum, &temp);

    _derniereExt.temperatureC = temp.temperature;
    _derniereExt.humiditeRH   = hum.relative_humidity;
    _derniereExt.humiditeAbs  = humiditeAbsolue(temp.temperature,
                                                  hum.relative_humidity);
    _derniereExt.valide       = true;
    _tsDerniereExt            = maintenant;

    return _derniereExt;
}

float BeetterSHT40::humiditeAbsolue(float tempC, float rh) {
    float es = 6.112f * expf((17.67f * tempC) / (tempC + 243.5f));
    return (rh / 100.0f) * es * 216.7f / (tempC + 273.15f);
}

bool BeetterSHT40::_lireRaw_int(float& tC, float& rh) {
    _swStart();
    if (!_swWrite((SHT40_ADDR << 1) | 0)) { _swStop(); return false; }
    _swWrite(SHT40_CMD_HRP);
    _swStop();
    delay(10);
    _swStart();
    if (!_swWrite((SHT40_ADDR << 1) | 1)) { _swStop(); return false; }
    uint8_t d[6];
    for (int i = 0; i < 6; i++) d[i] = _swRead(i < 5);
    _swStop();
    uint16_t rawT = ((uint16_t)d[0] << 8) | d[1];
    uint16_t rawH = ((uint16_t)d[3] << 8) | d[4];
    tC = -45.0f + 175.0f * (float)rawT / 65535.0f;
    rh = -6.0f  + 125.0f * (float)rawH / 65535.0f;
    if (rh < 0.0f)   rh = 0.0f;
    if (rh > 100.0f) rh = 100.0f;
    return true;
}

void BeetterSHT40::_sdaHigh() { pinMode(_swSDA, INPUT_PULLUP); }
void BeetterSHT40::_sdaLow()  { pinMode(_swSDA, OUTPUT); digitalWrite(_swSDA, LOW); }
void BeetterSHT40::_sclHigh() { pinMode(_swSCL, INPUT_PULLUP); }
void BeetterSHT40::_sclLow()  { pinMode(_swSCL, OUTPUT); digitalWrite(_swSCL, LOW); }
int  BeetterSHT40::_sdaRead() { return digitalRead(_swSDA); }

void BeetterSHT40::_swStart() {
    _sdaHigh(); _sclHigh(); delayMicroseconds(SW_DLY_US);
    _sdaLow();              delayMicroseconds(SW_DLY_US);
    _sclLow();              delayMicroseconds(SW_DLY_US);
}
void BeetterSHT40::_swStop() {
    _sdaLow();              delayMicroseconds(SW_DLY_US);
    _sclHigh();             delayMicroseconds(SW_DLY_US);
    _sdaHigh();             delayMicroseconds(SW_DLY_US);
}
bool BeetterSHT40::_swWrite(uint8_t b) {
    for (int i = 0; i < 8; i++) {
        if (b & 0x80) _sdaHigh(); else _sdaLow();
        b <<= 1;
        delayMicroseconds(SW_DLY_US);
        _sclHigh(); delayMicroseconds(SW_DLY_US);
        _sclLow();  delayMicroseconds(SW_DLY_US);
    }
    _sdaHigh(); delayMicroseconds(SW_DLY_US);
    _sclHigh(); delayMicroseconds(SW_DLY_US);
    bool ack = (_sdaRead() == LOW);
    _sclLow();  delayMicroseconds(SW_DLY_US);
    return ack;
}
uint8_t BeetterSHT40::_swRead(bool ack) {
    uint8_t b = 0;
    _sdaHigh();
    for (int i = 0; i < 8; i++) {
        b <<= 1;
        _sclHigh(); delayMicroseconds(SW_DLY_US);
        if (_sdaRead()) b |= 1;
        _sclLow();  delayMicroseconds(SW_DLY_US);
    }
    if (ack) _sdaLow(); else _sdaHigh();
    delayMicroseconds(SW_DLY_US);
    _sclHigh(); delayMicroseconds(SW_DLY_US);
    _sclLow();  delayMicroseconds(SW_DLY_US);
    _sdaHigh();
    return b;
}
