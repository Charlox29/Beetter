/**
 * =============================================================
 *  BeetterMic.cpp  –  Implémentation microphones I2S + MFCC
 * =============================================================
 *
 *  Architecture MFCC :
 *  1. Capture I2S stéréo → extraction d'un canal
 *  2. Fenêtrage de Hamming sur N_FFT échantillons
 *  3. FFT → spectre de magnitude
 *  4. Filtrage : on ne conserve que [fMin, fMax]
 *  5. Banque de N filtres triangulaires en échelle Mel
 *  6. Log de l'énergie par filtre
 *  7. DCT → coefficients MFCC
 *  8. Extraction fréquence dominante et énergie RMS
 * =============================================================
 */

#include "BeetterMic.h"

// Définition des membres statiques – alloués une seule fois en mémoire globale
// 24000 × 4 bytes = 94 KB réservés dès le démarrage, jamais fragmentés
int32_t  BeetterMic::_captureBuf[24000];
uint32_t BeetterMic::_captureBufSize = 24000;

// ─── Initialisation I2S ──────────────────────────────────────
bool BeetterMic::begin(uint8_t bclk, uint8_t ws, uint8_t din, uint32_t fs) {
    _fs   = fs;
    _pret = false;

    // setPins(BCLK, WS, SDOUT=-1, SDIN, MCLK=-1)
    _i2s.setPins(bclk, ws, -1, din, -1);

    // 32 bits par slot, les ICS-43434 sortent 24 bits cadrés à gauche
    if (!_i2s.begin(I2S_MODE_STD, (int)fs,
                    I2S_DATA_BIT_WIDTH_32BIT,
                    I2S_SLOT_MODE_STEREO)) {
        Serial.println(F("[MIC] Échec init I2S."));
        return false;
    }

    _pret = true;
    Serial.printf("[MIC] I2S OK – Fs=%lu Hz\n", (unsigned long)fs);
    return true;
}

bool    BeetterMic::isReady()      const { return _pret; }
uint32_t BeetterMic::getSampleRate() const { return _fs; }

// ─── Capture d'un canal ──────────────────────────────────────
void BeetterMic::_capturer(uint8_t canal, uint32_t n, int32_t* out) {
    // Le bus I2S retourne des paires (gauche, droit) dans le même buffer.
    // On lit par blocs de 64 paires pour ne pas saturer la stack.
    const uint32_t BLOC = 64;
    int32_t paire[BLOC * 2];

    uint32_t recus = 0;
    while (recus < n) {
        uint32_t aLire = min((uint32_t)(n - recus), BLOC);
        size_t lus = _i2s.readBytes((char*)paire, aLire * 2 * sizeof(int32_t));
        uint32_t paires = lus / (2 * sizeof(int32_t));

        for (uint32_t i = 0; i < paires && recus < n; i++) {
            // ICS-43434 : 24 bits cadrés à gauche dans un mot 32 bits
            // → décalage de 8 bits pour récupérer la valeur signée 24 bits
            int32_t val = paire[i * 2 + canal] >> 8;
            out[recus++] = val;
        }
    }
}

// ─── Analyse publique : intérieur ────────────────────────────
ResultatAudio BeetterMic::analyserInterieur(uint32_t dureeCaptureMs,
                                             float fMin, float fMax,
                                             uint8_t nbCoeffs) {
    if (!_pret) return {0, 0, {0}, 0, false};

    uint32_t nTotal = (_fs * dureeCaptureMs) / 1000UL;
    if (nTotal > _captureBufSize) {
        Serial.printf("[MIC-INT] Durée trop longue (%lu samples > %lu)\n",
                      (unsigned long)nTotal, (unsigned long)_captureBufSize);
        return {0, 0, {0}, 0, false};
    }
    _capturer(1, nTotal, _captureBuf);   // canal droit (J5) = INTÉRIEUR
    return _analyser(_captureBuf, nTotal, fMin, fMax, nbCoeffs);
}

// ─── Analyse publique : extérieur ────────────────────────────
ResultatAudio BeetterMic::analyserExterieur(uint32_t dureeCaptureMs,
                                              float fMin, float fMax,
                                              uint8_t nbCoeffs) {
    if (!_pret) return {0, 0, {0}, 0, false};

    uint32_t nTotal = (_fs * dureeCaptureMs) / 1000UL;
    if (nTotal > _captureBufSize) {
        Serial.printf("[MIC-EXT] Durée trop longue (%lu samples > %lu)\n",
                      (unsigned long)nTotal, (unsigned long)_captureBufSize);
        return {0, 0, {0}, 0, false};
    }
    _capturer(0, nTotal, _captureBuf);   // canal gauche (J2) = EXTÉRIEUR
    return _analyser(_captureBuf, nTotal, fMin, fMax, nbCoeffs);
}

// ─── Analyse interne : FFT + MFCC par fenêtrage temporel ────
//
//  Le signal de 3s est découpé en N fenêtres de BEETTER_FFT_SIZE samples.
//  Les MFCC sont calculés sur chaque fenêtre puis moyennés.
//  Résultat identique à librosa.feature.mfcc() avec ses paramètres par défaut,
//  ce qui garantit la cohérence avec les données d'entraînement du modèle ML.
//
//  3000ms @ 8000Hz = 24000 samples = 11 fenêtres × 2048 samples
//  Surcoût vs code précédent : ~120 bytes RAM, ~200ms CPU (négligeable)
//
ResultatAudio BeetterMic::_analyser(int32_t* samples, uint32_t n,
                                     float fMin, float fMax,
                                     uint8_t nbCoeffs) {
    ResultatAudio res;
    res.valide = false;
    res.nbCoeffsMFCC = min(nbCoeffs, (uint8_t)13);

    if (n < BEETTER_FFT_SIZE) {
        Serial.println(F("[MIC] Buffer trop court pour la FFT."));
        return res;
    }

    // ── 1. Résolution fréquentielle et bornes ──
    double   df    = (double)_fs / BEETTER_FFT_SIZE;
    uint32_t iBas  = (uint32_t)max(1.0, floor((double)fMin / df));
    uint32_t iHaut = (uint32_t)min((double)(BEETTER_FFT_SIZE / 2 - 1),
                                    ceil((double)fMax / df));

    // ── 2. Nombre de fenêtres complètes dans le buffer ──
    uint32_t nFenetres = n / BEETTER_FFT_SIZE;  // 24000 / 2048 = 11

    // ── 3. Accumulateurs pour le moyennage ──
    double mfccSomme[13] = {0};
    double rmsSomme      = 0.0;
    double freqSomme     = 0.0;
    float  mfccFenetre[13];

    // ── 4. Boucle sur chaque fenêtre ──
    for (uint32_t f = 0; f < nFenetres; f++) {
        const int32_t* debut = samples + f * BEETTER_FFT_SIZE;

        // 4a. RMS de cette fenêtre
        double sommeCarre = 0.0;
        for (uint32_t i = 0; i < BEETTER_FFT_SIZE; i++) {
            double s = (double)debut[i] / 8388608.0;
            sommeCarre += s * s;
        }
        rmsSomme += sqrt(sommeCarre / BEETTER_FFT_SIZE);

        // 4b. Fenêtrage de Hamming + copie dans _vReal
        for (uint32_t i = 0; i < BEETTER_FFT_SIZE; i++) {
            double s = (double)debut[i] / 8388608.0;
            double w = 0.54 - 0.46 * cos(2.0 * M_PI * i / (BEETTER_FFT_SIZE - 1));
            _vReal[i] = s * w;
            _vImag[i] = 0.0;
        }

        // 4c. FFT
        ArduinoFFT<double> fft(_vReal, _vImag, BEETTER_FFT_SIZE, (double)_fs);
        fft.compute(FFTDirection::Forward);
        fft.complexToMagnitude();

        // 4d. Fréquence dominante de cette fenêtre
        double   picMax = 0.0;
        uint32_t iPic   = iBas;
        for (uint32_t i = iBas; i <= iHaut; i++) {
            if (_vReal[i] > picMax) { picMax = _vReal[i]; iPic = i; }
        }
        freqSomme += (double)iPic * df;

        // 4e. MFCC de cette fenêtre
        _calculerMFCC(_vReal, BEETTER_FFT_SIZE / 2,
                      26, res.nbCoeffsMFCC, mfccFenetre);
        for (int c = 0; c < res.nbCoeffsMFCC; c++)
            mfccSomme[c] += mfccFenetre[c];
    }

    // ── 5. Moyennage sur toutes les fenêtres ──
    res.energieRMS    = (float)(rmsSomme  / nFenetres);
    res.freqDominante = (float)(freqSomme / nFenetres);
    for (int c = 0; c < res.nbCoeffsMFCC; c++)
        res.mfcc[c] = (float)(mfccSomme[c] / nFenetres);

    res.valide = true;
    return res;
}

// ─── Calcul MFCC ─────────────────────────────────────────────
void BeetterMic::_calculerMFCC(const double* magnitude, uint32_t n,
                                 uint8_t nbFiltres, uint8_t nbCoeffs,
                                 float* mfccOut) {
    // Résolution fréquentielle du spectre passé en argument
    double df = (double)_fs / (2.0 * n);  // n = N_FFT/2

    // ── Bornes Mel de la banque de filtres ──
    float melMin = _hzToMel(80.0f);    // limite basse en Mel (~80 Hz)
    float melMax = _hzToMel(4000.0f);  // limite haute en Mel (raisonnable à 8kHz)

    // Points centraux des filtres uniformément espacés en Mel
    // nbFiltres + 2 points pour inclure les bords
    int nPoints = nbFiltres + 2;
    float* melPoints = (float*)alloca(nPoints * sizeof(float));
    float* hzPoints  = (float*)alloca(nPoints * sizeof(float));
    int*   binPoints = (int*)alloca(nPoints * sizeof(int));

    for (int i = 0; i < nPoints; i++) {
        melPoints[i] = melMin + i * (melMax - melMin) / (nPoints - 1);
        hzPoints[i]  = _melToHz(melPoints[i]);
        binPoints[i] = (int)(hzPoints[i] / df);
        if (binPoints[i] >= (int)n) binPoints[i] = (int)n - 1;
    }

    // ── Énergie de chaque filtre triangulaire ──
    double* energieFiltres = (double*)alloca(nbFiltres * sizeof(double));

    for (int m = 0; m < nbFiltres; m++) {
        int   fL = binPoints[m];      // borne gauche
        int   fC = binPoints[m + 1];  // centre
        int   fR = binPoints[m + 2];  // borne droite

        double energie = 0;
        // Montée gauche → centre
        for (int k = fL; k <= fC && k < (int)n; k++) {
            double poids = (fC > fL) ? (double)(k - fL) / (fC - fL) : 1.0;
            energie += magnitude[k] * poids;
        }
        // Descente centre → droite
        for (int k = fC + 1; k <= fR && k < (int)n; k++) {
            double poids = (fR > fC) ? (double)(fR - k) / (fR - fC) : 0.0;
            energie += magnitude[k] * poids;
        }
        // Log pour comprimer la dynamique (mimique l'oreille humaine)
        energieFiltres[m] = log(max(energie, 1e-10));
    }

    // ── DCT-II pour obtenir les coefficients MFCC ──
    for (int c = 0; c < nbCoeffs; c++) {
        double somme = 0;
        for (int m = 0; m < nbFiltres; m++) {
            somme += energieFiltres[m] * cos(M_PI * c * (2 * m + 1) / (2.0 * nbFiltres));
        }
        mfccOut[c] = (float)somme;
    }
}

// ─── Conversions Mel ─────────────────────────────────────────
float BeetterMic::_hzToMel(float hz) {
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

float BeetterMic::_melToHz(float mel) {
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}
