/**
 * =============================================================
 *  BeetterPhoto.h  –  Bibliothèque photorésistance
 * =============================================================
 *  Matériel : Photorésistance 10kΩ en diviseur de tension
 *             Broche : GPIO0 (ADC 12 bits, 0..4095)
 *
 *  Montage conseillé :
 *    3.3V ── R_photo ── GPIO0 ── R_fixe(10kΩ) ── GND
 *  → Plus il fait clair, plus la tension sur GPIO0 est basse.
 *    (Inverser R_photo et R_fixe pour l'effet inverse.)
 *
 *  Fonctionnalités :
 *   - Lecture brute ADC
 *   - Lissage par moyenne glissante (N mesures)
 *   - Estimation du niveau de luminosité (obscur/ombre/lumière/soleil)
 *   - Gestion de l'intervalle de mesure
 * =============================================================
 */

#pragma once

#include <Arduino.h>

// ─── Niveaux de luminosité ───────────────────────────────────
enum NiveauLuminosite {
    LUM_NUIT    = 0,   // < 200
    LUM_OMBRE   = 1,   // 200..1000
    LUM_NUAGEUX = 2,   // 1000..2500
    LUM_SOLEIL  = 3    // > 2500
};

class BeetterPhoto {
public:
    /**
     * Initialise la broche ADC.
     * @param pin          Broche GPIO (doit être capable d'ADC)
     * @param resolution   Résolution ADC en bits (10, 11 ou 12)
     * @param nbMoyenne    Nombre de lectures à moyenner (1..16)
     */
    void begin(uint8_t pin, uint8_t resolution = 12, uint8_t nbMoyenne = 4);

    /**
     * Lit la luminosité.
     * @param intervalle_ms  Durée minimale entre deux lectures effectives.
     *                       Retourne la dernière valeur si pas encore écoulé.
     * @return Valeur ADC brute [0..4095 en 12 bits]
     */
    uint16_t lire(uint32_t intervalle_ms = 10000);

    /**
     * Retourne la tension correspondante (V).
     * @param adcBrut  Valeur lue par lire()
     * @param vcc      Tension de référence (défaut 3.3V)
     */
    float tension(uint16_t adcBrut, float vcc = 3.3f) const;

    /**
     * Catégorise le niveau de luminosité.
     * @param adcBrut  Valeur ADC brute
     */
    NiveauLuminosite niveau(uint16_t adcBrut) const;

    /** Retourne la dernière valeur sans nouvelle lecture */
    uint16_t derniereLecture() const;

private:
    uint8_t       _pin       = 0;
    uint8_t       _nbMoy     = 4;
    uint16_t      _maxADC    = 4095;
    uint16_t      _derniere  = 0;
    unsigned long _tsLecture = 0;
};

// ─── Implémentation inline (fichier unique) ──────────────────

inline void BeetterPhoto::begin(uint8_t pin, uint8_t resolution, uint8_t nbMoyenne) {
    _pin    = pin;
    _nbMoy  = constrain(nbMoyenne, 1, 16);
    _maxADC = (1 << resolution) - 1;

    analogReadResolution(resolution);
    Serial.printf("[PHOTO] Init GPIO%d, résolution %d bits, moyenne sur %d lectures\n",
                  pin, resolution, _nbMoy);
}

inline uint16_t BeetterPhoto::lire(uint32_t intervalle_ms) {
    unsigned long maintenant = millis();
    if (_tsLecture != 0 && (maintenant - _tsLecture) < intervalle_ms) {
        return _derniere;   // retourne la dernière valeur en cache
    }

    // Moyenne sur N lectures pour réduire le bruit
    uint32_t somme = 0;
    for (uint8_t i = 0; i < _nbMoy; i++) {
        somme += analogRead(_pin);
        delayMicroseconds(200);  // petit délai entre lectures
    }
    _derniere  = (uint16_t)(somme / _nbMoy);
    _tsLecture = maintenant;
    return _derniere;
}

inline float BeetterPhoto::tension(uint16_t adcBrut, float vcc) const {
    return adcBrut * (vcc / (float)_maxADC);
}

inline NiveauLuminosite BeetterPhoto::niveau(uint16_t adcBrut) const {
    if (adcBrut < 200)  return LUM_NUIT;
    if (adcBrut < 1000) return LUM_OMBRE;
    if (adcBrut < 2500) return LUM_NUAGEUX;
    return LUM_SOLEIL;
}

inline uint16_t BeetterPhoto::derniereLecture() const { return _derniere; }
