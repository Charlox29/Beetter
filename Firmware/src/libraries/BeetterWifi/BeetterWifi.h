/**
 * =============================================================
 *  BeetterWifi.h  –  Bibliothèque WiFi + Bluetooth ESP32-C6
 * =============================================================
 *  L'ESP32-C6 embarque :
 *   - WiFi 802.11 b/g/n (2.4 GHz) + WiFi 6 (802.11ax)
 *   - Bluetooth 5.0 LE
 *
 *  Fonctionnalités WiFi :
 *   - Connexion à un réseau (avec retry automatique)
 *   - Scan des réseaux disponibles
 *   - Obtention de l'IP / RSSI
 *   - Déconnexion propre
 *
 *  Fonctionnalités Bluetooth LE :
 *   - Serveur BLE simple avec une caractéristique de notification
 *   - Envoi de chaînes de texte (ex. dump JSON des mesures)
 *   - Utile pour la configuration terrain sans WiFi
 *
 *  ⚠️  BLE requiert le core arduino-esp32 >= 3.0.0 et la lib
 *       NimBLE-Arduino (optionnel mais recommandé pour économiser la RAM) :
 *       Arduino Library Manager → "NimBLE-Arduino"
 * =============================================================
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>

// Décommentez la ligne ci-dessous si NimBLE-Arduino est installé
// #define BEETTER_USE_NIMBLE

#ifdef BEETTER_USE_NIMBLE
  #include <NimBLEDevice.h>
#else
  #include <BLEDevice.h>
  #include <BLEServer.h>
  #include <BLEUtils.h>
  #include <BLE2902.h>
#endif

// ─── UUIDs du service BLE Beetter ────────────────────────────
//  Ces UUIDs sont personnalisés – ne pas modifier entre Hive et l'app.
#define BEETTER_BLE_SERVICE_UUID  "12345678-1234-1234-1234-123456789abc"
#define BEETTER_BLE_CHAR_UUID     "abcdef01-1234-1234-1234-123456789abc"

class BeetterWifi {
public:

    // ══════════════════════════════════════════
    //  WIFI
    // ══════════════════════════════════════════

    /**
     * Tente de se connecter au réseau WiFi.
     * @param ssid       Nom du réseau
     * @param password   Mot de passe
     * @param tentatives Nombre de tentatives avant abandon (défaut 20)
     * @param delaiMs    Délai entre tentatives en ms (défaut 500)
     * @return true si connecté
     */
    bool connecter(const char* ssid, const char* password,
                   uint8_t tentatives = 20, uint32_t delaiMs = 500);

    /** Déconnecte le WiFi proprement */
    void deconnecter();

    /** @return true si le WiFi est connecté */
    bool estConnecte() const;

    /**
     * Affiche les informations de connexion (SSID, IP, RSSI, canal).
     */
    void afficherInfo() const;

    /**
     * Scanne les réseaux WiFi disponibles et les affiche sur Serial.
     * @return Nombre de réseaux trouvés
     */
    int scannerReseaux();

    /** @return Adresse IP locale sous forme de chaîne */
    String adresseIP() const;

    /** @return RSSI WiFi courant en dBm */
    int rssi() const;

    // ══════════════════════════════════════════
    //  BLUETOOTH LE
    // ══════════════════════════════════════════

    /**
     * Initialise et démarre le serveur BLE.
     * @param nomAppareil  Nom annoncé (ex. "Beetter-Hive")
     * @return true si démarré
     */
    bool demarrerBLE(const char* nomAppareil = "Beetter-Hive");

    /**
     * Envoie une notification BLE à tous les clients connectés.
     * @param message  Chaîne à envoyer (ex. JSON des mesures)
     * @return true si au moins un client est abonné
     */
    bool envoyerBLE(const String& message);

    /** Arrête le serveur BLE et libère la mémoire */
    void arreterBLE();

    /** @return true si au moins un client BLE est connecté */
    bool clientBLEConnecte() const;

    /** @return Nombre de clients BLE connectés */
    uint8_t nbClientsBLE() const;

private:
    bool _wifiConnecte = false;
    bool _bleActif     = false;

#ifdef BEETTER_USE_NIMBLE
    NimBLEServer*         _bleServeur = nullptr;
    NimBLECharacteristic* _bleCaract  = nullptr;
    NimBLEAdvertising*    _bleAdv     = nullptr;
#else
    BLEServer*            _bleServeur = nullptr;
    BLECharacteristic*    _bleCaract  = nullptr;
    BLEAdvertising*       _bleAdv     = nullptr;
#endif

    // Callback de connexion/déconnexion BLE
    struct CallbackServeur;
    uint8_t _nbClients = 0;
};

// ═══════════════════════════════════════════════════════════
//  Implémentation (fichier unique pour simplifier l'intégration
//  Arduino sans .cpp séparé)
// ═══════════════════════════════════════════════════════════

// ─── WiFi ────────────────────────────────────────────────────

inline bool BeetterWifi::connecter(const char* ssid, const char* password,
                                    uint8_t tentatives, uint32_t delaiMs) {
    if (estConnecte()) deconnecter();

    Serial.printf("[WiFi] Connexion à \"%s\"", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    for (uint8_t i = 0; i < tentatives; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            _wifiConnecte = true;
            Serial.println();
            afficherInfo();
            return true;
        }
        delay(delaiMs);
        Serial.print('.');
    }

    Serial.println(F("\n[WiFi] Échec de connexion."));
    _wifiConnecte = false;
    return false;
}

inline void BeetterWifi::deconnecter() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _wifiConnecte = false;
    Serial.println(F("[WiFi] Déconnecté."));
}

inline bool BeetterWifi::estConnecte() const {
    return WiFi.status() == WL_CONNECTED;
}

inline void BeetterWifi::afficherInfo() const {
    if (!estConnecte()) { Serial.println(F("[WiFi] Non connecté.")); return; }
    Serial.printf("[WiFi] SSID: %s  IP: %s  RSSI: %d dBm  Canal: %d\n",
                  WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(),
                  WiFi.channel());
}

inline int BeetterWifi::scannerReseaux() {
    Serial.println(F("[WiFi] Scan des réseaux..."));
    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println(F("[WiFi] Aucun réseau trouvé."));
    } else {
        Serial.printf("[WiFi] %d réseau(x) trouvé(s) :\n", n);
        for (int i = 0; i < n; i++) {
            Serial.printf("  %2d. %-32s  %4d dBm  Canal %2d  %s\n",
                          i + 1,
                          WiFi.SSID(i).c_str(),
                          WiFi.RSSI(i),
                          WiFi.channel(i),
                          WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Ouvert" : "Chiffré");
        }
    }
    WiFi.scanDelete();
    return n;
}

inline String BeetterWifi::adresseIP() const {
    return WiFi.localIP().toString();
}

inline int BeetterWifi::rssi() const {
    return WiFi.RSSI();
}

// ─── Bluetooth LE ────────────────────────────────────────────

#ifndef BEETTER_USE_NIMBLE

// Callback de gestion des connexions (bibliothèque BLE standard esp32)
class _BeetterBLECallback : public BLEServerCallbacks {
public:
    uint8_t* _nbClients;
    _BeetterBLECallback(uint8_t* nb) : _nbClients(nb) {}

    void onConnect(BLEServer* srv) override {
        (*_nbClients)++;
        Serial.printf("[BLE] Client connecté (total: %d)\n", *_nbClients);
        // Permettre plusieurs connexions simultanées
        srv->getAdvertising()->start();
    }
    void onDisconnect(BLEServer* srv) override {
        if (*_nbClients > 0) (*_nbClients)--;
        Serial.printf("[BLE] Client déconnecté (restant: %d)\n", *_nbClients);
    }
};

inline bool BeetterWifi::demarrerBLE(const char* nomAppareil) {
    if (_bleActif) return true;

    BLEDevice::init(nomAppareil);
    _bleServeur = BLEDevice::createServer();
    _bleServeur->setCallbacks(new _BeetterBLECallback(&_nbClients));

    BLEService* service = _bleServeur->createService(BEETTER_BLE_SERVICE_UUID);
    _bleCaract = service->createCharacteristic(
        BEETTER_BLE_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ  |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _bleCaract->addDescriptor(new BLE2902());  // requis pour NOTIFY

    service->start();
    _bleAdv = BLEDevice::getAdvertising();
    _bleAdv->addServiceUUID(BEETTER_BLE_SERVICE_UUID);
    _bleAdv->setScanResponse(true);
    _bleAdv->start();

    _bleActif = true;
    Serial.printf("[BLE] Serveur démarré – appareil : \"%s\"\n", nomAppareil);
    return true;
}

inline bool BeetterWifi::envoyerBLE(const String& message) {
    if (!_bleActif || !_bleCaract) return false;
    if (_nbClients == 0) return false;

    _bleCaract->setValue(message.c_str());
    _bleCaract->notify();
    return true;
}

inline void BeetterWifi::arreterBLE() {
    if (!_bleActif) return;
    BLEDevice::deinit(true);
    _bleActif  = false;
    _nbClients = 0;
    Serial.println(F("[BLE] Arrêté."));
}

inline bool    BeetterWifi::clientBLEConnecte() const { return _nbClients > 0; }
inline uint8_t BeetterWifi::nbClientsBLE()      const { return _nbClients; }

#else  // ─── Version NimBLE ────────────────────────────────────

inline bool BeetterWifi::demarrerBLE(const char* nomAppareil) {
    if (_bleActif) return true;
    NimBLEDevice::init(nomAppareil);
    _bleServeur = NimBLEDevice::createServer();
    NimBLEService* service = _bleServeur->createService(BEETTER_BLE_SERVICE_UUID);
    _bleCaract = service->createCharacteristic(
        BEETTER_BLE_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    service->start();
    _bleAdv = NimBLEDevice::getAdvertising();
    _bleAdv->addServiceUUID(BEETTER_BLE_SERVICE_UUID);
    _bleAdv->start();
    _bleActif = true;
    Serial.printf("[BLE/NimBLE] Démarré – \"%s\"\n", nomAppareil);
    return true;
}

inline bool BeetterWifi::envoyerBLE(const String& message) {
    if (!_bleActif || !_bleCaract) return false;
    if (_bleServeur->getConnectedCount() == 0) return false;
    _bleCaract->setValue(message.c_str());
    _bleCaract->notify();
    return true;
}

inline void    BeetterWifi::arreterBLE()              { NimBLEDevice::deinit(true); _bleActif = false; }
inline bool    BeetterWifi::clientBLEConnecte() const { return _bleServeur && _bleServeur->getConnectedCount() > 0; }
inline uint8_t BeetterWifi::nbClientsBLE()      const { return _bleServeur ? _bleServeur->getConnectedCount() : 0; }

#endif  // BEETTER_USE_NIMBLE
