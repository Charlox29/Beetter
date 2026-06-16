/**
 * =============================================================
 *  BeetterSHT40.h  –  Bibliothèque capteurs T°/Humidité SHT40
 * =============================================================
 *  Matériel : 2× SEN0428 (Sensirion SHT40)
 *   - Capteur #1 EXTÉRIEUR (J1) : bus I2C matériel (Wire)     SDA=GPIO6, SCL=GPIO7
 *   - Capteur #2 INTÉRIEUR (J3) : bus I2C logiciel (bit-bang) SDA=GPIO10, SCL=GPIO11
 *
 *  Les deux capteurs ont l'adresse 0x44 → buses séparées obligatoires.
 *
 *  Dépendances : Adafruit SHT4x library + Adafruit BusIO
 * =============================================================
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>

struct MesureSHT40 {
    float   temperatureC;
    float   humiditeRH;
    float   humiditeAbs;
    bool    valide;
};

class BeetterSHT40 {
public:
    /**
     * @param sda0/scl0  Bus matériel  → capteur EXTÉRIEUR (J1)
     * @param sda1/scl1  Bus logiciel  → capteur INTÉRIEUR (J3)
     */
    bool begin(uint8_t sda0, uint8_t scl0,
               uint8_t sda1, uint8_t scl1);

    /** Lit le capteur INTÉRIEUR (bus logiciel – J3) */
    MesureSHT40 lireInterieur(uint32_t intervalle_ms = 10000);

    /** Lit le capteur EXTÉRIEUR (bus matériel – J1) */
    MesureSHT40 lireExterieur(uint32_t intervalle_ms = 10000);

    bool interieurPret() const;
    bool exterieurPret() const;

    static float humiditeAbsolue(float tempC, float rh);

private:
    // ── Capteur EXTÉRIEUR (J1) : bus matériel ──
    Adafruit_SHT4x _sht_ext;
    bool           _ok_ext = false;
    MesureSHT40    _derniereExt;
    unsigned long  _tsDerniereExt = 0;

    // ── Capteur INTÉRIEUR (J3) : bus logiciel ──
    uint8_t _swSDA = 0, _swSCL = 0;
    bool    _ok_int = false;
    MesureSHT40   _derniereInt;
    unsigned long _tsDerniereInt = 0;

    static constexpr uint8_t SHT40_ADDR    = 0x44;
    static constexpr uint8_t SHT40_CMD_HRP = 0xFD;
    static constexpr uint8_t SW_DLY_US     = 5;

    void    _sdaHigh();
    void    _sdaLow();
    void    _sclHigh();
    void    _sclLow();
    int     _sdaRead();
    void    _swStart();
    void    _swStop();
    bool    _swWrite(uint8_t b);
    uint8_t _swRead(bool ack);
    bool    _lireRaw_int(float& tC, float& rh);
};
