/**
 * =============================================================
 *  BeetterSD.h  –  Gestion carte micro-SD (ADA4682)
 * =============================================================
 *  Optimisation : les fichiers CSV sont ouverts une seule fois
 *  par cycle via beginCycle()/endCycle() pour minimiser les
 *  open/close FAT (~50ms chacun).
 * =============================================================
 */

#pragma once
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

class BeetterSD {
public:
    bool begin(uint8_t sck, uint8_t miso, uint8_t mosi,
               uint8_t cs, uint8_t det);

    bool isReady()       const;
    bool cartePresente();
    uint64_t espaceTotal();
    uint64_t espaceUtilise();

    /**
     * Ouvre les 3 fichiers CSV en mode append.
     * À appeler une fois en début de cycle d'écriture.
     */
    bool beginCycle();

    /**
     * Ferme les 3 fichiers ouverts par beginCycle().
     * À appeler en fin de cycle d'écriture.
     */
    void endCycle();

    /** Écrit une ligne dans env.csv (beginCycle() doit être appelé avant). */
    bool ecrireEnv(const char* ligne);

    /** Écrit une ligne dans mfcc_int.csv. */
    bool ecrireMfccInt(const char* ligne);

    /** Écrit une ligne dans mfcc_ext.csv. */
    bool ecrireMfccExt(const char* ligne);

    // Méthodes utilitaires conservées
    bool lireFichier(const char* chemin, String& sortie);
    bool supprimerFichier(const char* chemin);
    void listerRacine();

    // Pour l'init des en-têtes CSV (utilisé une seule fois)
    bool ecrireLigne(const char* chemin, const char* ligne);

private:
    uint8_t  _cs  = 0;
    uint8_t  _det = 0;
    bool     _pret = false;

    // Fichiers ouverts pendant un cycle
    File _fEnv;
    File _fMfccInt;
    File _fMfccExt;
    bool _cycleOuvert = false;
};
