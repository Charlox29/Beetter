/**
 * =============================================================
 *  BeetterLoRa.h  –  Protocole multi-blocs Beetter
 * =============================================================
 *  2 blocs par trame, magics distincts, tailles fixes :
 *
 *  BLOC ENV – magic 0xE0 – 23 bytes
 *  offset  0 : uint8   magic = 0xE0
 *  offset  1 : uint16  seq
 *  offset  3 : char[4] hive_id
 *  offset  7 : uint32  timestamp (Unix UTC)
 *  offset 11 : int16   temp_int  (°C × 100)
 *  offset 13 : uint16  hum_int   (%RH × 10)
 *  offset 15 : int16   temp_ext  (°C × 100)
 *  offset 17 : uint16  hum_ext   (%RH × 10)
 *  offset 19 : uint16  light_ext (indice × 10, ex. 69 → 6.9/10)
 *  offset 21 : uint16  crc16
 *
 *  BLOC AUD – magic 0xA0 – 73 bytes
 *  offset  0 : uint8   magic = 0xA0
 *  offset  1 : uint16  seq
 *  offset  3 : char[4] hive_id
 *  offset  7 : uint32  timestamp
 *  offset 11 : uint16  freq_int  (Hz × 10)
 *  offset 13 : uint16  rms_int   (× 10000)
 *  offset 15 : uint16  freq_ext  (Hz × 10)
 *  offset 17 : uint16  rms_ext   (× 10000)
 *  offset 19 : int16[13] mfcc_int (× 100)
 *  offset 45 : int16[13] mfcc_ext (× 100)
 *  offset 71 : uint16  crc16
 * =============================================================
 */

#pragma once
#include <Arduino.h>
#include <RH_RF95.h>

#define MAGIC_ENV    0xE0
#define MAGIC_AUD    0xA0
#define SIZE_ENV     23
#define SIZE_AUD     73

// Duty cycle ETSI 868 MHz – sous-bande g1 (1% max)
// ToA mesuré : ~176ms (overhead UART Grove ATmega168 inclus)
// DC pratique : 180ms / 30000ms = 0.60% ✓ (bien en dessous du 1%)
#define LORA_TOA_MS            180UL              // ms, ToA mesuré terrain
#define LORA_INTERVALLE_MIN_MS (LORA_TOA_MS * 99UL)  // 17820ms – garde-fou légal

#pragma pack(push, 1)
struct BlocENV {
    uint8_t  magic;
    uint16_t seq;
    char     hiveId[4];
    uint32_t timestamp;
    int16_t  tempInt;      // °C × 100
    uint16_t humInt;       // %RH × 10
    int16_t  tempExt;      // °C × 100
    uint16_t humExt;       // %RH × 10
    uint16_t lightExt;     // indice × 10
    uint16_t crc16;
};

struct BlocAUD {
    uint8_t  magic;
    uint16_t seq;
    char     hiveId[4];
    uint32_t timestamp;
    uint16_t freqInt;      // Hz × 10
    uint16_t rmsInt;       // × 10000
    uint16_t freqExt;      // Hz × 10
    uint16_t rmsExt;       // × 10000
    int16_t  mfccInt[13];  // × 100
    int16_t  mfccExt[13];  // × 100
    uint16_t crc16;
};
#pragma pack(pop)

static_assert(sizeof(BlocENV) == SIZE_ENV, "BlocENV taille incorrecte");
static_assert(sizeof(BlocAUD) == SIZE_AUD, "BlocAUD taille incorrecte");

class BeetterLoRa {
public:
    bool begin(uint8_t rxPin, uint8_t txPin, float frequence = 868.0f);

    /** Envoie ENV + AUD concaténés en une seule trame (96 bytes) */
    bool envoyerTrame(const char* hiveId, uint32_t ts, uint32_t seq,
                      float tempInt, float humInt,
                      float tempExt, float humExt,
                      float lightIndice,
                      float freqInt, float rmsInt,
                      const float* mfccInt, uint8_t nbMfccInt,
                      float freqExt, float rmsExt,
                      const float* mfccExt, uint8_t nbMfccExt);

    bool     isReady()   const;
    int      rssiDernier();
    uint32_t tempsAvantProchainEnvoi() const;
    float    dutyCycleUtilise()        const;

    static uint16_t crc16(const uint8_t* data, size_t len);

private:
    bool     _pret           = false;
    float    _freq           = 868.0f;
    uint32_t _tsDernierEnvoi = 0;
    uint32_t _tmsEmission    = 0;
    uint32_t _tsDebutHeure   = 0;

    static HardwareSerial          _ss;
    static RH_RF95<HardwareSerial> _radio;

    bool _envoyer(const uint8_t* buf, uint8_t len, const char* label);
    void _comptabiliser(uint32_t dureeMs);
};
