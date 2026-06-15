/**
 * =============================================================
 *  BeetterSD.cpp  –  Implémentation gestion carte micro-SD
 * =============================================================
 */

#include "BeetterSD.h"

// ─── Initialisation ──────────────────────────────────────────
bool BeetterSD::begin(uint8_t sck, uint8_t miso, uint8_t mosi,
                      uint8_t cs, uint8_t det) {
    _cs  = cs;
    _det = det;
    _pret = false;

    // Broche de détection (pull-up intégré sur le module ADA4682)
    pinMode(_det, INPUT);

    if (!cartePresente()) {
        Serial.println(F("[SD] Aucune carte détectée."));
        return false;
    }

    // Initialisation du bus SPI dédié à la SD
    SPI.begin(sck, miso, mosi, cs);

    if (!SD.begin(cs)) {
        Serial.println(F("[SD] Échec init. Vérifier câblage et tension 3.3V."));
        return false;
    }

    // Affiche les infos de la carte
    uint8_t type = SD.cardType();
    const char* nomType =
        (type == CARD_MMC)  ? "MMC"  :
        (type == CARD_SD)   ? "SDSC" :
        (type == CARD_SDHC) ? "SDHC" : "INCONNU";

    Serial.printf(F("[SD] Prête – Type : %s  |  Taille : %llu Mo\n"),
                  nomType, SD.cardSize() / (1024ULL * 1024ULL));

    _pret = true;
    return true;
}

// ─── État ────────────────────────────────────────────────────
bool BeetterSD::isReady()       const { return _pret; }
bool BeetterSD::cartePresente()       { return digitalRead(_det) == HIGH; }
uint64_t BeetterSD::espaceTotal()     { return _pret ? SD.totalBytes() : 0; }
uint64_t BeetterSD::espaceUtilise()   { return _pret ? SD.usedBytes()  : 0; }

// ─── Écriture ────────────────────────────────────────────────
bool BeetterSD::ecrireLigne(const char* chemin, const String& ligne) {
    if (!_pret) return false;

    // FILE_APPEND = ouvre en ajout, crée le fichier s'il n'existe pas
    File f = SD.open(chemin, FILE_APPEND);
    if (!f) {
        Serial.printf(F("[SD] Impossible d'ouvrir %s en écriture.\n"), chemin);
        return false;
    }

    f.println(ligne);   // println ajoute \r\n
    f.close();
    return true;
}

// ─── Lecture ─────────────────────────────────────────────────
bool BeetterSD::lireFichier(const char* chemin, String& sortie) {
    if (!_pret) return false;

    File f = SD.open(chemin, FILE_READ);
    if (!f) {
        Serial.printf(F("[SD] Fichier introuvable : %s\n"), chemin);
        return false;
    }

    sortie = "";
    while (f.available()) sortie += (char)f.read();
    f.close();
    return true;
}

// ─── Suppression ─────────────────────────────────────────────
bool BeetterSD::supprimerFichier(const char* chemin) {
    if (!_pret) return false;

    if (!SD.exists(chemin)) {
        Serial.printf(F("[SD] Fichier %s inexistant.\n"), chemin);
        return false;
    }

    return SD.remove(chemin);
}

// ─── Listage de la racine ────────────────────────────────────
void BeetterSD::listerRacine() {
    if (!_pret) { Serial.println(F("[SD] SD non initialisée.")); return; }

    File racine = SD.open("/");
    if (!racine) { Serial.println(F("[SD] Impossible d'ouvrir la racine.")); return; }

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
