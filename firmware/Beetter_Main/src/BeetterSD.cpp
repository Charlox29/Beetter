/**
 * BeetterSD.cpp  –  Gestion carte micro-SD
 */

#include "BeetterSD.h"
#include "../BeetterConfig.h"

bool BeetterSD::begin(uint8_t sck, uint8_t miso, uint8_t mosi,
                      uint8_t cs, uint8_t det) {
    _cs  = cs;
    _det = det;
    _pret = false;

    pinMode(_det, INPUT);
    if (!cartePresente()) {
        Serial.println(F("[SD] Aucune carte détectée."));
        return false;
    }

    SPI.begin(sck, miso, mosi, cs);
    if (!SD.begin(cs)) {
        Serial.println(F("[SD] Echec init."));
        return false;
    }

    uint8_t type = SD.cardType();
    const char* nomType =
        (type == CARD_MMC)  ? "MMC"  :
        (type == CARD_SD)   ? "SDSC" :
        (type == CARD_SDHC) ? "SDHC" : "INCONNU";
    Serial.printf("[SD] Prete – %s  %llu Mo\n",
                  nomType, SD.cardSize() / (1024ULL * 1024ULL));
    _pret = true;
    return true;
}

bool     BeetterSD::isReady()       const { return _pret; }
bool     BeetterSD::cartePresente()       { return digitalRead(_det) == HIGH; }
uint64_t BeetterSD::espaceTotal()         { return _pret ? SD.totalBytes() : 0; }
uint64_t BeetterSD::espaceUtilise()       { return _pret ? SD.usedBytes()  : 0; }

// ─── Gestion de cycle ────────────────────────────────────────
bool BeetterSD::beginCycle() {
    if (!_pret || _cycleOuvert) return false;

    // Vérifier que la carte est toujours présente (retrait à chaud)
    if (!cartePresente()) {
        Serial.println(F("[SD] Carte absente – cycle SD ignore."));
        _pret = false;   // désactiver jusqu'au prochain redémarrage
        return false;
    }

    _fEnv     = SD.open(SD_LOG_ENV,      FILE_APPEND);
    _fMfccInt = SD.open(SD_LOG_MFCC_INT, FILE_APPEND);
    _fMfccExt = SD.open(SD_LOG_MFCC_EXT, FILE_APPEND);
    _cycleOuvert = (_fEnv && _fMfccInt && _fMfccExt);
    if (!_cycleOuvert)
        Serial.println(F("[SD] Echec ouverture fichiers CSV."));
    return _cycleOuvert;
}

void BeetterSD::endCycle() {
    if (!_cycleOuvert) return;
    if (_fEnv)     { _fEnv.close(); }
    if (_fMfccInt) { _fMfccInt.close(); }
    if (_fMfccExt) { _fMfccExt.close(); }
    _cycleOuvert = false;
}

bool BeetterSD::ecrireEnv(const char* ligne) {
    if (!_cycleOuvert || !_fEnv) return false;
    _fEnv.println(ligne);
    return true;
}

bool BeetterSD::ecrireMfccInt(const char* ligne) {
    if (!_cycleOuvert || !_fMfccInt) return false;
    _fMfccInt.println(ligne);
    return true;
}

bool BeetterSD::ecrireMfccExt(const char* ligne) {
    if (!_cycleOuvert || !_fMfccExt) return false;
    _fMfccExt.println(ligne);
    return true;
}

// ─── Utilitaires ─────────────────────────────────────────────
bool BeetterSD::ecrireLigne(const char* chemin, const char* ligne) {
    if (!_pret) return false;
    File f = SD.open(chemin, FILE_APPEND);
    if (!f) return false;
    f.println(ligne);
    f.close();
    return true;
}

bool BeetterSD::lireFichier(const char* chemin, String& sortie) {
    if (!_pret) return false;
    File f = SD.open(chemin, FILE_READ);
    if (!f) return false;
    sortie = "";
    while (f.available()) sortie += (char)f.read();
    f.close();
    return true;
}

bool BeetterSD::supprimerFichier(const char* chemin) {
    if (!_pret || !SD.exists(chemin)) return false;
    return SD.remove(chemin);
}

void BeetterSD::listerRacine() {
    if (!_pret) { Serial.println(F("[SD] SD non initialisee.")); return; }
    File racine = SD.open("/");
    if (!racine) return;
    Serial.println(F("[SD] ── Contenu de la racine ──"));
    for (File f = racine.openNextFile(); f; f = racine.openNextFile()) {
        Serial.printf("  %-24s %s  %u o\n",
                      f.name(),
                      f.isDirectory() ? "[DIR]" : "     ",
                      (unsigned)f.size());
        f.close();
    }
    Serial.println(F("[SD] ─────────────────────────"));
    racine.close();
}
