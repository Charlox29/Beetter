/**
 * =============================================================
 *  BeetterSD.h  –  Bibliothèque gestion carte micro-SD
 * =============================================================
 *  Matériel : Adafruit 4682 (ADA4682) – bus SPI 3.3V
 *  Fonctions :
 *   - Initialisation avec détection de carte
 *   - Écriture de lignes CSV horodatées
 *   - Listage / suppression de fichiers
 *   - Vérification de l'espace libre
 * =============================================================
 */

#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

class BeetterSD {
public:
    /**
     * Initialise le bus SPI et la carte SD.
     * @param sck   Broche SPI SCK
     * @param miso  Broche SPI MISO
     * @param mosi  Broche SPI MOSI
     * @param cs    Broche Chip Select
     * @param det   Broche de détection de carte (HIGH = carte présente)
     * @return true si la carte est prête, false sinon
     */
    bool begin(uint8_t sck, uint8_t miso, uint8_t mosi,
               uint8_t cs, uint8_t det);

    /** @return true si la carte SD est initialisée et disponible */
    bool isReady() const;

    /**
     * Écrit une ligne texte dans un fichier CSV (crée le fichier si besoin).
     * Un retour chariot est ajouté automatiquement.
     * @param chemin  Ex. "/env.csv"
     * @param ligne   Chaîne à écrire
     * @return true si succès
     */
    bool ecrireLigne(const char* chemin, const String& ligne);

    /**
     * Lit le contenu complet d'un fichier et le renvoie dans `sortie`.
     * @param chemin  Chemin du fichier
     * @param sortie  Référence vers la String de destination
     * @return true si le fichier existe et a été lu
     */
    bool lireFichier(const char* chemin, String& sortie);

    /**
     * Supprime un fichier.
     * @param chemin  Chemin du fichier à supprimer
     * @return true si supprimé
     */
    bool supprimerFichier(const char* chemin);

    /**
     * Affiche sur Serial la liste des fichiers à la racine
     * avec leur taille.
     */
    void listerRacine();

    /**
     * @return Espace total de la carte en octets (0 si non initialisée)
     */
    uint64_t espaceTotal();

    /**
     * @return Espace utilisé en octets
     */
    uint64_t espaceUtilise();

    /**
     * Vérifie l'état de la broche de détection de carte.
     * @return true si une carte est physiquement présente
     */
    bool cartePresente();

private:
    uint8_t _cs  = 0;
    uint8_t _det = 0;
    bool    _pret = false;
};
