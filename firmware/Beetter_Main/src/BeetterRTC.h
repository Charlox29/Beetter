/**
 * =============================================================
 *  BeetterRTC.h  –  Bibliothèque RTC PCF8523
 * =============================================================
 *  Matériel : ADA3295 (Adafruit PCF8523 Real Time Clock)
 *  Bus      : I2C matériel (Wire) – partagé avec SHT40 #1
 *             SDA=GPIO6, SCL=GPIO7
 *
 *  Fonctionnalités :
 *   - Initialisation et vérification de l'état du RTC
 *   - Lecture de la date/heure courante
 *   - Réglage manuel de la date/heure
 *   - Synchronisation automatique depuis NTP via WiFi
 *   - Horodatage de mesures (timestamp Unix)
 *   - Formatage lisible pour les logs CSV
 *
 *  Dépendances : RTClib (by Adafruit)
 * =============================================================
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"

class BeetterRTC {
public:
    /**
     * Initialise le RTC sur le bus Wire déjà configuré.
     * Wire.begin() doit avoir été appelé avant cette méthode.
     * @return true si le RTC répond
     */
    bool begin();

    /**
     * Retourne la date/heure courante.
     * @return DateTime (objet RTClib)
     */
    DateTime maintenant();

    /**
     * Retourne le timestamp Unix courant (secondes depuis 01/01/1970).
     * Pratique pour l'horodatage des mesures.
     */
    uint32_t timestamp();

    /**
     * Règle le RTC manuellement.
     * @param annee   Ex. 2025
     * @param mois    1..12
     * @param jour    1..31
     * @param heure   0..23
     * @param minute  0..59
     * @param seconde 0..59
     */
    void regler(uint16_t annee, uint8_t mois, uint8_t jour,
                uint8_t heure, uint8_t minute, uint8_t seconde);

    /**
     * Règle le RTC à la date/heure de compilation du firmware.
     * Utile lors du premier flash.
     */
    void reglerDepuisCompilation();

    /**
     * Synchronise le RTC depuis un serveur NTP.
     * ⚠️ Le WiFi doit être connecté avant d'appeler cette méthode.
     * @param serveurNTP     Ex. "pool.ntp.org"
     * @param offsetGMT_sec  Décalage UTC en secondes (ex. 3600 pour UTC+1)
     * @param offsetDST_sec  Heure d'été en secondes (ex. 3600)
     * @return true si la synchronisation a réussi
     */
    bool syncNTP(const char* serveurNTP,
                 long offsetGMT_sec, int offsetDST_sec);

    /**
     * Retourne la date/heure formatée pour les fichiers CSV.
     * Format : "YYYY-MM-DD HH:MM:SS"
     * @param buf   Buffer de sortie (doit faire au moins 20 caractères)
     */
    void formaterISO(char* buf);

    /**
     * Affiche sur Serial la date/heure courante.
     */
    void afficher();

    /**
     * @return true si le RTC a perdu l'alimentation (pile morte ou premier démarrage).
     *         Dans ce cas, l'heure est incorrecte et doit être reconfigurée.
     */
    bool alimentationPerdue();

    /** @return true si le RTC est initialisé et répond */
    bool isReady() const;

private:
    RTC_PCF8523 _rtc;
    bool        _pret = false;
};
