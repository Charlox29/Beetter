/**
 * =============================================================
 *  BeetterMic.h  –  Bibliothèque microphones I2S + MFCC
 * =============================================================
 *  Matériel : 2× Adafruit 6049 (ICS-43434) en configuration stéréo
 *   - Canal GAUCHE (SEL=GND)   → Micro INTÉRIEUR (J2)
 *   - Canal DROIT  (SEL=3.3V)  → Micro EXTÉRIEUR (J5)
 *
 *  Fonctionnalités :
 *   - Capture audio I2S configurable (durée, Fs)
 *   - FFT via arduinoFFT (à installer)
 *   - Calcul MFCC (N coefficients, banque de filtres Mel)
 *   - Extraction de la fréquence dominante dans une plage donnée
 *   - Calcul d'énergie RMS
 *
 *  Dépendances : ESP_I2S (core esp32), arduinoFFT
 *
 *  ⚠️  arduinoFFT à installer via Arduino Library Manager :
 *      "arduinoFFT" par Enrique Condes (version >= 2.0.0)
 * =============================================================
 */

#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>
#include <arduinoFFT.h>
#include <math.h>

// ─── Taille FFT (puissance de 2, adaptée à 8kHz / 3s) ───────
//  N_FFT = 2048 → résolution fréquentielle = Fs/N_FFT = 3.9 Hz
#define BEETTER_FFT_SIZE  2048

// ─── Résultats d'une analyse acoustique ─────────────────────
struct ResultatAudio {
    float   freqDominante;              // Hz – pic principal dans [fMin, fMax]
    float   energieRMS;                 // amplitude RMS normalisée [0..1]
    float   mfcc[13];                   // coefficients MFCC (jusqu'à 13)
    uint8_t nbCoeffsMFCC;               // nombre effectif de coefficients
    bool    valide;                     // true si la capture a réussi
};

// ─── Classe principale ───────────────────────────────────────
class BeetterMic {
public:
    /**
     * Initialise le bus I2S.
     * @param bclk  Broche BCLK (SCK)
     * @param ws    Broche WS (LRCL)
     * @param din   Broche DIN (DOUT des micros)
     * @param fs    Fréquence d'échantillonnage souhaitée (Hz)
     * @return true si I2S initialisé
     */
    bool begin(uint8_t bclk, uint8_t ws, uint8_t din, uint32_t fs = 8000);

    /**
     * Analyse le canal intérieur (gauche).
     *
     * @param dureeCaptureMs  Durée de capture audio en ms (ex. 3000)
     * @param fMin            Fréquence basse d'intérêt en Hz (ex. 50)
     * @param fMax            Fréquence haute d'intérêt en Hz (ex. 600)
     * @param nbCoeffs        Nombre de coefficients MFCC (1..13, défaut 13)
     * @return ResultatAudio
     */
    ResultatAudio analyserInterieur(uint32_t dureeCaptureMs,
                                    float fMin, float fMax,
                                    uint8_t nbCoeffs = 13);

    /**
     * Analyse le canal extérieur (droit).
     * Mêmes paramètres qu'analyserInterieur.
     */
    ResultatAudio analyserExterieur(uint32_t dureeCaptureMs,
                                    float fMin, float fMax,
                                    uint8_t nbCoeffs = 13);

    /** @return Fréquence d'échantillonnage configurée (Hz) */
    uint32_t getSampleRate() const;

    /** @return true si le bus I2S est prêt */
    bool isReady() const;

private:
    I2SClass  _i2s;
    uint32_t  _fs   = 8000;
    bool      _pret = false;

    // Buffers FFT (alloués dans la pile au moment de l'analyse)
    double _vReal[BEETTER_FFT_SIZE];
    double _vImag[BEETTER_FFT_SIZE];

    /**
     * Capture N échantillons du canal demandé.
     * @param canal  0 = gauche (intérieur), 1 = droite (extérieur)
     * @param n      Nombre d'échantillons
     * @param out    Buffer de sortie (int32_t)
     */
    void _capturer(uint8_t canal, uint32_t n, int32_t* out);

    /**
     * Calcule le résultat complet (FFT + MFCC) sur un buffer de samples.
     * @param samples    Tableau d'échantillons bruts (int32_t, 24 bits utiles)
     * @param n          Nombre d'échantillons
     * @param fMin       Hz
     * @param fMax       Hz
     * @param nbCoeffs   Nombre de coefficients MFCC
     * @return ResultatAudio
     */
    ResultatAudio _analyser(int32_t* samples, uint32_t n,
                             float fMin, float fMax,
                             uint8_t nbCoeffs);

    /**
     * Calcule les coefficients MFCC.
     * @param magnitude  Spectre de magnitude (taille N_FFT/2)
     * @param n          N_FFT/2
     * @param nbFiltres  Nombre de filtres Mel
     * @param nbCoeffs   Nombre de coefficients cepstraux
     * @param mfccOut    Buffer de sortie
     */
    void _calculerMFCC(const double* magnitude, uint32_t n,
                        uint8_t nbFiltres, uint8_t nbCoeffs,
                        float* mfccOut);

    /** Conversion Hz → Mel */
    static float _hzToMel(float hz);
    /** Conversion Mel → Hz */
    static float _melToHz(float mel);
};
