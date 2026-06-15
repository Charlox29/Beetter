/**
 * =============================================================
 *  BeetterLoRa.h  –  Bibliothèque transmission LoRa (binaire)
 * =============================================================
 *  Format : 1 trame binaire FIXE de 83 bytes par cycle
 *  Conformité : ETSI EN 300 220 – Duty cycle 1% sur 868 MHz
 *
 *  ┌──────────────────┬────────┬───────┬─────────────────────┐
 *  │ Champ            │ Offset │ Bytes │ Encodage            │
 *  ├──────────────────┼────────┼───────┼─────────────────────┤
 *  │ magic            │      0 │     1 │ 0xBE (fixe)         │
 *  │ seq              │      1 │     2 │ uint16 – compteur   │
 *  │ hive_id          │      3 │     4 │ char[4] ex. "B001"  │
 *  │ timestamp        │      7 │     4 │ uint32 Unix         │
 *  │ temp_int         │     11 │     2 │ int16  = °C × 100   │
 *  │ hum_int          │     13 │     2 │ uint16 = %RH × 10   │
 *  │ temp_ext         │     15 │     2 │ int16  = °C × 100   │
 *  │ hum_ext          │     17 │     2 │ uint16 = %RH × 10   │
 *  │ lum              │     19 │     2 │ uint16 ADC 0..4095  │
 *  │ freq_int         │     21 │     2 │ uint16 = Hz × 10    │
 *  │ rms_int          │     23 │     2 │ uint16 = RMS × 10000│
 *  │ freq_ext         │     25 │     2 │ uint16 = Hz × 10    │
 *  │ rms_ext          │     27 │     2 │ uint16 = RMS × 10000│
 *  │ mfcc_int[0..12]  │     29 │    26 │ int16[13] × 100     │
 *  │ mfcc_ext[0..12]  │     55 │    26 │ int16[13] × 100     │
 *  │ crc16            │     81 │     2 │ uint16 CRC-16/CCITT │
 *  ├──────────────────┼────────┼───────┼─────────────────────┤
 *  │ TOTAL            │        │    83 │ marge : 168 bytes   │
 *  └──────────────────┴────────┴───────┴─────────────────────┘
 *
 *  Time on Air (SF7, BW125, CR4/5) : ~98 ms
 *  Intervalle minimum légal (1% DC) : ~10 s
 *  → LORA_SEND_INTERVAL_MS réglé à 20s dans BeetterConfig.h
 *    pour une marge confortable (DC réel : 0.16%)
 *
 *  Décodage Python côté Beetter Home :
 *  ─────────────────────────────────
 *  import struct
 *
 *  MAGIC  = 0xBE
 *  FORMAT = '<BH4sIhHhHHHHHH13h13hH'
 *  SIZE   = 83
 *
 *  def decoder(payload: bytes) -> dict:
 *      if len(payload) != SIZE or payload[0] != MAGIC:
 *          raise ValueError('Trame invalide')
 *      v = struct.unpack(FORMAT, payload)
 *      # Vérification CRC (optionnel mais recommandé)
 *      # crc_recu = v[39]
 *      # crc_calc = crc16(payload[:-2])
 *      return {
 *          'seq':      v[1],
 *          'id':       v[2].decode('ascii').rstrip('\x00'),
 *          'ts':       v[3],
 *          'temp_int': v[4]  / 100,
 *          'hum_int':  v[5]  / 10,
 *          'temp_ext': v[6]  / 100,
 *          'hum_ext':  v[7]  / 10,
 *          'lum':      v[8],
 *          'freq_int': v[9]  / 10,
 *          'rms_int':  v[10] / 10000,
 *          'freq_ext': v[11] / 10,
 *          'rms_ext':  v[12] / 10000,
 *          'mfcc_int': [x / 100 for x in v[13:26]],
 *          'mfcc_ext': [x / 100 for x in v[26:39]],
 *      }
 *
 *  ⚠️  Bibliothèque requise (ZIP) :
 *  https://github.com/Seeed-Studio/Grove_LoRa_433MHz_and_915MHz_RF
 * =============================================================
 */

#pragma once

#include <Arduino.h>
#include <RH_RF95.h>

// ─── Constantes du protocole binaire ─────────────────────────
#define BEETTER_MAGIC        0xBE   // Octet de synchronisation
#define BEETTER_FRAME_SIZE   83     // Taille fixe de la trame (bytes)

// ─── Duty cycle ETSI 868 MHz ─────────────────────────────────
// Time on Air trame 83 bytes (SF7, BW125, CR4/5) : ~98 ms
// Silence minimum légal (1%) : 98 × 99 = ~9702 ms (~10 s)
// On utilise 60s dans BeetterConfig.h pour une large marge.
#define LORA_TOA_MS              180UL  // ToA réel mesuré (176ms + marge)
#define LORA_INTERVALLE_MIN_MS   (LORA_TOA_MS * 99UL)  // ~9702 ms

// ─── Structure de la trame binaire ───────────────────────────
// Attribut packed : pas de padding entre les champs
// → la structure correspond octet pour octet à la trame LoRa
#pragma pack(push, 1)
struct TrameBeetter {
    uint8_t  magic;           // 0xBE
    uint16_t seq;             // Numéro de séquence
    char     hiveId[4];       // "B001" (pas de null terminal)
    uint32_t timestamp;       // Unix

    int16_t  tempInt;         // °C × 100
    uint16_t humInt;          // %RH × 10
    int16_t  tempExt;         // °C × 100
    uint16_t humExt;          // %RH × 10
    uint16_t luminosite;      // ADC brut 0..4095

    uint16_t freqDomInt;      // Hz × 10
    uint16_t energieRMSInt;   // × 10000
    uint16_t freqDomExt;      // Hz × 10
    uint16_t energieRMSExt;   // × 10000

    int16_t  mfccInt[13];     // × 100
    int16_t  mfccExt[13];     // × 100

    uint16_t crc16;           // CRC-16/CCITT des bytes 0..80
};
#pragma pack(pop)

// Vérification statique de la taille à la compilation
static_assert(sizeof(TrameBeetter) == BEETTER_FRAME_SIZE,
              "Taille TrameBeetter incorrecte – verifier l'alignement");

// ─── Helpers de remplissage ──────────────────────────────────
// Fonctions inline pour convertir les valeurs flottantes
// en entiers encodés dans la structure TrameBeetter.
namespace Beetter {
    /** Remplit une TrameBeetter depuis les valeurs brutes des capteurs */
    inline void remplir(TrameBeetter& t,
                        const char*  hiveId,
                        uint32_t     timestamp,
                        uint16_t     seq,
                        // SHT40
                        float tempInt, float humInt,
                        float tempExt, float humExt,
                        // Photo
                        uint16_t luminosite,
                        // Micro int
                        float freqInt, float rmsInt,
                        const float* mfccInt, uint8_t nbMFCCInt,
                        // Micro ext
                        float freqExt, float rmsExt,
                        const float* mfccExt, uint8_t nbMFCCExt)
    {
        t.magic     = BEETTER_MAGIC;
        t.seq       = seq;
        memset(t.hiveId, 0, 4);
        strncpy(t.hiveId, hiveId, 4);
        t.timestamp = timestamp;

        t.tempInt        = (int16_t)(tempInt * 100);
        t.humInt         = (uint16_t)(humInt  * 10);
        t.tempExt        = (int16_t)(tempExt * 100);
        t.humExt         = (uint16_t)(humExt  * 10);
        t.luminosite     = luminosite;

        t.freqDomInt     = (uint16_t)(freqInt * 10);
        t.energieRMSInt  = (uint16_t)(rmsInt  * 10000);
        t.freqDomExt     = (uint16_t)(freqExt * 10);
        t.energieRMSExt  = (uint16_t)(rmsExt  * 10000);

        for (int i = 0; i < 13; i++) {
            t.mfccInt[i] = (i < nbMFCCInt) ? (int16_t)(mfccInt[i] * 100) : 0;
            t.mfccExt[i] = (i < nbMFCCExt) ? (int16_t)(mfccExt[i] * 100) : 0;
        }
        // CRC calculé par BeetterLoRa::envoyerTrame() avant envoi
        t.crc16 = 0;
    }
}

// ─── Classe LoRa ─────────────────────────────────────────────
class BeetterLoRa {
public:
    /**
     * Initialise le module LoRa.
     * @param rxPin     GPIO RX ESP32 (→ TX du module Grove)
     * @param txPin     GPIO TX ESP32 (→ RX du module Grove)
     * @param frequence MHz (ex. 868.0)
     */
    bool begin(uint8_t rxPin, uint8_t txPin, float frequence = 868.0f);

    /**
     * Calcule le CRC16, sérialise la structure en binaire
     * et envoie la trame de 83 bytes.
     *
     * Refuse l'envoi si le duty cycle 1% serait dépassé.
     *
     * @return true si envoi réussi
     */
    bool envoyerTrame(TrameBeetter& trame);

    /**
     * Temps restant avant que le prochain envoi soit légal (ms).
     * 0 = envoi possible immédiatement.
     */
    uint32_t tempsAvantProchainEnvoi() const;

    /** Duty cycle utilisé sur la dernière heure (%) */
    float dutyCycleUtilise() const;

    /** Écoute un message entrant pendant timeoutMs ms */
    bool recevoirMessage(uint8_t* buf, uint8_t& len,
                         uint32_t timeoutMs = 2000);

    /** RSSI du dernier paquet reçu (dBm) */
    int  rssiDernier();

    /** @return true si initialisé */
    bool isReady() const;

    /**
     * Affiche la trame sur Serial (debug).
     * Montre les bytes hex + les valeurs décodées.
     */
    static void afficherTrame(const TrameBeetter& t);

    /**
     * Calcule le CRC-16/CCITT (polynôme 0x1021, init 0xFFFF).
     * Appliqué sur les bytes 0..80 (tout sauf le champ crc16).
     */
    static uint16_t calculerCRC16(const uint8_t* data, size_t len);

private:
    bool     _pret             = false;
    float    _freq             = 868.0f;
    uint32_t _tsDernierEnvoi   = 0;
    uint32_t _tmsEmissionHeure = 0;
    uint32_t _tsDebutHeure     = 0;

    static HardwareSerial          _ss;
    static RH_RF95<HardwareSerial> _radio;

    void _comptabiliserEmission(uint32_t dureeMs);
};
