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

    // Nombre d'échantillons correspondant à la durée demandée
    uint32_t nTotal = (_fs * dureeCaptureMs) / 1000UL;
    // Allocation dynamique pour éviter de saturer la stack ESP32
    int32_t* buf = (int32_t*)malloc(nTotal * sizeof(int32_t));
    if (!buf) {
        Serial.println(F("[MIC] Mémoire insuffisante pour la capture."));
        return {0, 0, {0}, 0, false};
    }

    _capturer(1, nTotal, buf);   // 1 = canal droit (J5, SEL=3.3V) = INTERIEUR
    ResultatAudio res = _analyser(buf, nTotal, fMin, fMax, nbCoeffs);
    free(buf);
    return res;
}

// ─── Analyse publique : extérieur ────────────────────────────
ResultatAudio BeetterMic::analyserExterieur(uint32_t dureeCaptureMs,
                                              float fMin, float fMax,
                                              uint8_t nbCoeffs) {
    if (!_pret) return {0, 0, {0}, 0, false};

    uint32_t nTotal = (_fs * dureeCaptureMs) / 1000UL;
    int32_t* buf = (int32_t*)malloc(nTotal * sizeof(int32_t));
    if (!buf) {
        Serial.println(F("[MIC] Mémoire insuffisante pour la capture."));
        return {0, 0, {0}, 0, false};
    }

    _capturer(0, nTotal, buf);   // 0 = canal gauche (J2, SEL=GND) = EXTERIEUR
    ResultatAudio res = _analyser(buf, nTotal, fMin, fMax, nbCoeffs);
    free(buf);
    return res;
}

// ─── Analyse interne : FFT + MFCC ────────────────────────────
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

    // ── 1. Calcul de l'énergie RMS sur tous les échantillons ──
    double sommeCarre = 0;
    for (uint32_t i = 0; i < n; i++) {
        double s = (double)samples[i] / 8388608.0;  // normalisation 24 bits
        sommeCarre += s * s;
    }
    res.energieRMS = (float)sqrt(sommeCarre / n);

    // ── 2. Préparation des buffers FFT (fenêtre centrale) ──
    //  On prend les BEETTER_FFT_SIZE premiers échantillons (déjà représentatifs
    //  pour une analyse de 3s – on peut moyenner plusieurs fenêtres si besoin)
    for (uint32_t i = 0; i < BEETTER_FFT_SIZE; i++) {
        double s = (double)samples[i] / 8388608.0;
        // Fenêtre de Hamming : réduit les effets de bord
        double w = 0.54 - 0.46 * cos(2.0 * M_PI * i / (BEETTER_FFT_SIZE - 1));
        _vReal[i] = s * w;
        _vImag[i] = 0.0;
    }

    // ── 3. FFT ──
    ArduinoFFT<double> fft(_vReal, _vImag, BEETTER_FFT_SIZE, (double)_fs);
    fft.compute(FFTDirection::Forward);
    fft.complexToMagnitude();
    // Après complexToMagnitude(), _vReal contient le spectre de magnitude.

    // ── 4. Résolution fréquentielle ──
    //  Δf = Fs / N_FFT
    double df = (double)_fs / BEETTER_FFT_SIZE;

    // Indices correspondant à [fMin, fMax]
    uint32_t iBas  = (uint32_t)max(1.0, floor((double)fMin / df));
    uint32_t iHaut = (uint32_t)min((double)(BEETTER_FFT_SIZE / 2 - 1),
                                    ceil((double)fMax / df));

    // ── 5. Fréquence dominante dans [fMin, fMax] ──
    double   picMax  = 0;
    uint32_t iPic    = iBas;
    for (uint32_t i = iBas; i <= iHaut; i++) {
        if (_vReal[i] > picMax) { picMax = _vReal[i]; iPic = i; }
    }
    res.freqDominante = (float)(iPic * df);

    // ── 6. Calcul MFCC ──
    //  On passe uniquement la moitié positive du spectre.
    //  26 filtres Mel est la valeur standard pour l'acoustique apicole.
    //  Ce nombre est indépendant de BeetterConfig.h pour éviter les
    //  problèmes de portée entre bibliothèque et sketch.
    _calculerMFCC(_vReal, BEETTER_FFT_SIZE / 2,
                  26, res.nbCoeffsMFCC,
                  res.mfcc);

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
