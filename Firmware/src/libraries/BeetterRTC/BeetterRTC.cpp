/**
 * =============================================================
 *  BeetterRTC.cpp  –  Implémentation RTC PCF8523
 * =============================================================
 */

#include "BeetterRTC.h"
#include <WiFi.h>
#include "esp_sntp.h"

// ─── Initialisation ──────────────────────────────────────────
bool BeetterRTC::begin() {
    _pret = false;

    if (!_rtc.begin(&Wire)) {
        Serial.println(F("[RTC] Échec : PCF8523 introuvable sur I2C. Vérifier SDA/SCL."));
        return false;
    }

    // Vérifier si le RTC a perdu son alimentation
    if (alimentationPerdue()) {
        Serial.println(F("[RTC] Avertissement : alimentation perdue ou premier démarrage."));
        Serial.println(F("[RTC] Réglage automatique à la date de compilation."));
        reglerDepuisCompilation();
    } else {
        Serial.println(F("[RTC] Prêt."));
    }

    // Désactiver le mode SQW si activé (sortie timer non nécessaire ici)
    _rtc.writeSqwPinMode(PCF8523_OFF);

    _pret = true;
    return true;
}

// ─── Accesseurs ──────────────────────────────────────────────
bool     BeetterRTC::isReady()     const { return _pret; }
DateTime BeetterRTC::maintenant()        { return _rtc.now(); }
uint32_t BeetterRTC::timestamp()         { return _rtc.now().unixtime(); }

bool BeetterRTC::alimentationPerdue() {
    // initialized() retourne false si le RTC n'a jamais été réglé
    // lostPower() retourne true si la pile est vide
    return !_rtc.initialized() || _rtc.lostPower();
}

// ─── Réglage manuel ──────────────────────────────────────────
void BeetterRTC::regler(uint16_t annee, uint8_t mois, uint8_t jour,
                         uint8_t heure, uint8_t minute, uint8_t seconde) {
    _rtc.adjust(DateTime(annee, mois, jour, heure, minute, seconde));
    Serial.printf("[RTC] Réglé à %04d-%02d-%02d %02d:%02d:%02d\n",
                  annee, mois, jour, heure, minute, seconde);
}

void BeetterRTC::reglerDepuisCompilation() {
    // __DATE__ et __TIME__ contiennent la date/heure de compilation
    _rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println(F("[RTC] Réglé à la date de compilation."));
}

// ─── Synchronisation NTP ─────────────────────────────────────
bool BeetterRTC::syncNTP(const char* serveurNTP,
                          long offsetGMT_sec, int offsetDST_sec) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[RTC] NTP : WiFi non connecté."));
        return false;
    }

    Serial.print(F("[RTC] Synchronisation NTP..."));
    configTime(offsetGMT_sec, offsetDST_sec, serveurNTP);

    // Attendre que le temps soit disponible (max 10 s)
    struct tm timeinfo;
    int tentatives = 0;
    while (!getLocalTime(&timeinfo) && tentatives < 20) {
        delay(500);
        Serial.print('.');
        tentatives++;
    }

    if (tentatives >= 20) {
        Serial.println(F(" ÉCHEC (timeout)."));
        return false;
    }

    // Appliquer l'heure NTP au RTC matériel
    time_t now = time(nullptr);
    DateTime dt((uint32_t)now);
    _rtc.adjust(dt);

    Serial.printf(" OK – %04d-%02d-%02d %02d:%02d:%02d\n",
                  dt.year(), dt.month(), dt.day(),
                  dt.hour(), dt.minute(), dt.second());
    return true;
}

// ─── Formatage ISO 8601 ──────────────────────────────────────
void BeetterRTC::formaterISO(char* buf) {
    if (!_pret) { strcpy(buf, "0000-00-00 00:00:00"); return; }
    DateTime now = _rtc.now();
    snprintf(buf, 20, "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
}

// ─── Affichage Serial ────────────────────────────────────────
void BeetterRTC::afficher() {
    char buf[20];
    formaterISO(buf);
    Serial.printf("[RTC] %s (Unix: %lu)\n", buf, (unsigned long)timestamp());
}
