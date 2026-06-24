/**
 * =============================================================
 *  BeetterRTC.cpp  –  Implémentation RTC PCF8523
 * =============================================================
 */

#include "BeetterRTC.h"
#include "../BeetterConfig.h"
#include <WiFi.h>
#include "esp_sntp.h"
#include <sys/time.h>   // settimeofday()
#include <time.h>       // time(), gmtime_r(), strftime()

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

uint32_t BeetterRTC::timestamp() {
    // Timestamp robuste : si la lecture I2C est corrompue, on retombe
    // sur l'horloge système (calée sur le RTC au démarrage), plutôt que
    // de propager une date aberrante dans les trames LoRa.
    DateTime now;
    if (lireValide(now)) return now.unixtime();
    return (uint32_t)time(nullptr);
}

// ─── Lecture validée ─────────────────────────────────────────
bool BeetterRTC::lireValide(DateTime& out) {
    if (!_pret) return false;
    DateTime n = _rtc.now();
    // Garde-fou contre les trames I2C corrompues (bus humide / parasité)
    if (n.year()  < 2024 || n.year()  > 2099 ||
        n.month() < 1    || n.month() > 12   ||
        n.day()   < 1    || n.day()   > 31   ||
        n.hour()  > 23   || n.minute() > 59  || n.second() > 59) {
        return false;
    }
    out = n;
    return true;
}

// ─── Calage de l'horloge système sur le RTC ──────────────────
void BeetterRTC::synchroniserHorlogeSysteme() {
    DateTime now;
    if (!lireValide(now)) {
        Serial.println(F("[RTC] Horloge systeme NON calee (lecture RTC invalide)."));
        return;
    }
    struct timeval tv = { .tv_sec = (time_t)now.unixtime(), .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    setenv("TZ", "UTC0", 1);   // RTC en UTC → dates FAT en UTC (cohérent noms/CSV)
    tzset();
    Serial.printf("[RTC] Horloge systeme calee sur le RTC (UTC) : "
                  "%04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
}

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
    // __DATE__ et __TIME__ = heure locale de compilation (fuseau de la machine qui compile)
    // On soustrait UTC_OFFSET_SEC pour stocker l'heure UTC dans le RTC.
    // UTC_OFFSET_SEC (BeetterConfig.h) doit être ajusté selon la saison :
    // UTC+1 en hiver (3600s) ou UTC+2 en été (7200s) pour la France.
    DateTime local(F(__DATE__), F(__TIME__));
    DateTime utc(local.unixtime() - UTC_OFFSET_SEC);
    _rtc.adjust(utc);
    Serial.printf("[RTC] Regle a la compilation (UTC) : %04d-%02d-%02d %02d:%02d:%02d\n",
                  utc.year(), utc.month(), utc.day(),
                  utc.hour(), utc.minute(), utc.second());
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

    // Recaler l'horloge système ESP32 + TZ sur le RTC fraîchement réglé
    // (cohérence des dates FAT avec les noms de fichiers).
    synchroniserHorlogeSysteme();

    Serial.printf(" OK – %04d-%02d-%02d %02d:%02d:%02d\n",
                  dt.year(), dt.month(), dt.day(),
                  dt.hour(), dt.minute(), dt.second());
    return true;
}

// ─── Formatage ISO 8601 ──────────────────────────────────────
void BeetterRTC::formaterISO(char* buf) {
    DateTime now;
    if (_pret && lireValide(now)) {
        snprintf(buf, 20, "%04d-%02d-%02d %02d:%02d:%02d",
                 now.year(), now.month(), now.day(),
                 now.hour(), now.minute(), now.second());
    } else {
        // RTC absent ou lecture I2C corrompue → repli sur l'horloge système
        // (calée sur le RTC au démarrage, insensible aux glitches I2C).
        // Évite d'écrire une date aberrante dans les CSV.
        time_t t = time(nullptr);
        struct tm tm_utc;
        gmtime_r(&t, &tm_utc);
        strftime(buf, 20, "%Y-%m-%d %H:%M:%S", &tm_utc);
    }
}

// ─── Affichage Serial ────────────────────────────────────────
void BeetterRTC::afficher() {
    char buf[20];
    formaterISO(buf);
    Serial.printf("[RTC] %s (Unix: %lu)\n", buf, (unsigned long)timestamp());
}
