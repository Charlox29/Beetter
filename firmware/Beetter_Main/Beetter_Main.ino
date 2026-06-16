/**
 * =============================================================
 *  Beetter_Main.ino  –  Programme principal Beetter Hive
 * =============================================================
 *  Format LoRa : trame binaire fixe 83 bytes (1 envoi/cycle)
 * =============================================================
 */

#include "BeetterConfig.h"
#include "src/BeetterSD.h"
#include "src/BeetterSHT40.h"
#include "src/BeetterMic.h"
#include "src/BeetterRTC.h"
#include "src/BeetterLoRa.h"
#include "src/BeetterPhoto.h"
#include "src/BeetterWifi.h"

// ─── Instances ───────────────────────────────────────────────
BeetterSD     sd;
BeetterSHT40  sht;
BeetterMic    mic;
BeetterRTC    rtc;
BeetterLoRa   lora;
BeetterPhoto  photo;
BeetterWifi   wifi;

// ─── Dernières mesures ───────────────────────────────────────
MesureSHT40   mesureInt  = {0, 0, 0, false};
MesureSHT40   mesureExt  = {0, 0, 0, false};
uint16_t      luminosite = 0;
ResultatAudio audioInt   = {0, 0, {0}, 0, false};
ResultatAudio audioExt   = {0, 0, {0}, 0, false};
uint16_t      seqLoRa    = 0;   // Numéro de séquence LoRa

// ─── Timestamps des tâches ───────────────────────────────────
unsigned long _tsDernierSHT40  = 0;
unsigned long _tsDernierePhoto = 0;
unsigned long _tsDernierMicInt = 0;
unsigned long _tsDernierMicExt = 0;
unsigned long _tsDernierLoRa   = 0;

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    while (!Serial) {}
    delay(500);

    Serial.println(F("\n╔════════════════════════════════╗"));
    Serial.println(F("║    BEETTER HIVE – Démarrage    ║"));
    Serial.printf (  "║    Ruche : %-20s║\n", BEETTER_HIVE_ID);
    Serial.println(F("╚════════════════════════════════╝\n"));

    // 1. Carte SD
    if (!sd.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS, SD_DET)) {
        Serial.println(F("[WARN] SD non disponible."));
    } else {
        sd.listerRacine();
        _initFichiersCSV();
    }

    // 2. RTC + I2C matériel
    Wire.begin(I2C0_SDA, I2C0_SCL);
    if (!rtc.begin()) Serial.println(F("[WARN] RTC absent."));

    // 3. SHT40 ×2
    if (!sht.begin(I2C0_SDA, I2C0_SCL, SW_SDA, SW_SCL))
        Serial.println(F("[WARN] Aucun SHT40 trouvé."));

    // 4. Microphones I2S
    uint32_t fsMax = max(MIC_INT_SAMPLE_RATE_HZ, MIC_EXT_SAMPLE_RATE_HZ);
    if (!mic.begin(I2S_BCLK, I2S_WS, I2S_DIN, fsMax))
        Serial.println(F("[WARN] Micros I2S non disponibles."));

    // 5. Photorésistance
    photo.begin(PHOTO_PIN, 12, 4);

    // 6. WiFi + NTP
    bool wifiOk = wifi.connecter(WIFI_SSID, WIFI_PASSWORD, 15, 500);
    if (wifiOk && rtc.isReady())
        rtc.syncNTP(NTP_SERVER, NTP_GMT_OFFSET_SEC, NTP_DST_OFFSET_SEC);

    // 7. Bluetooth LE
    wifi.demarrerBLE(BT_DEVICE_NAME);

    // 8. LoRa
    if (!lora.begin(LORA_RX_PIN, LORA_TX_PIN, LORA_FREQ))
        Serial.println(F("[WARN] Module LoRa non disponible."));

    Serial.println(F("\n[BEETTER] Démarrage de la collecte.\n"));
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════
void loop() {
    unsigned long maintenant = millis();

    if (maintenant - _tsDernierSHT40  >= SHT40_SAMPLE_INTERVAL_MS)  { _tsDernierSHT40  = maintenant; _tacheSHT40(); }
    if (maintenant - _tsDernierePhoto >= PHOTO_SAMPLE_INTERVAL_MS)   { _tsDernierePhoto = maintenant; _tachePhoto(); }
    if (maintenant - _tsDernierMicInt >= MIC_INT_INTERVAL_MS)        { _tsDernierMicInt = maintenant; _tacheMicInterieur(); }
    if (maintenant - _tsDernierMicExt >= MIC_EXT_INTERVAL_MS)        { _tsDernierMicExt = maintenant; _tacheMicExterieur(); }
    if (maintenant - _tsDernierLoRa   >= LORA_SEND_INTERVAL_MS)      { _tsDernierLoRa   = maintenant; _tacheLoRa(); }

    delay(1);
}

// ════════════════════════════════════════════════════════════
//  TÂCHES
// ════════════════════════════════════════════════════════════

void _tacheSHT40() {
    mesureInt = sht.lireInterieur(SHT40_SAMPLE_INTERVAL_MS);
    mesureExt = sht.lireExterieur(SHT40_SAMPLE_INTERVAL_MS);
    if (!mesureInt.valide && !mesureExt.valide) return;

    char ts[20]; rtc.formaterISO(ts);
    String ligne = String(ts) + ","
                 + String(mesureInt.temperatureC, 2) + ","
                 + String(mesureInt.humiditeRH,   1) + ","
                 + String(mesureInt.humiditeAbs,  2) + ","
                 + String(mesureExt.temperatureC, 2) + ","
                 + String(mesureExt.humiditeRH,   1) + ","
                 + String(mesureExt.humiditeAbs,  2);
    sd.ecrireLigne(SD_LOG_ENV, ligne);

    Serial.printf("[ENV] Int: %.2f°C %.1f%%RH  |  Ext: %.2f°C %.1f%%RH\n",
                  mesureInt.temperatureC, mesureInt.humiditeRH,
                  mesureExt.temperatureC, mesureExt.humiditeRH);
}

void _tachePhoto() {
    luminosite = photo.lire(PHOTO_SAMPLE_INTERVAL_MS);
    Serial.printf("[PHOTO] Brut: %d  Tension: %.3fV  Niveau: %s\n",
                  luminosite, photo.tension(luminosite),
                  _nomNiveau(photo.niveau(luminosite)));
}

void _tacheMicInterieur() {
    if (!mic.isReady()) return;
    Serial.println(F("[MIC-INT] Capture..."));
    audioInt = mic.analyserInterieur(MIC_INT_CAPTURE_MS,
                                      MIC_INT_FREQ_MIN_HZ,
                                      MIC_INT_FREQ_MAX_HZ,
                                      MFCC_NUM_COEFFS);
    if (!audioInt.valide) { Serial.println(F("[MIC-INT] Invalide.")); return; }
    Serial.printf("[MIC-INT] Freq: %.1f Hz  RMS: %.4f\n",
                  audioInt.freqDominante, audioInt.energieRMS);

    char ts[20]; rtc.formaterISO(ts);
    String ligne = String(ts) + "," + String(audioInt.freqDominante, 1)
                 + "," + String(audioInt.energieRMS, 4);
    for (int i = 0; i < audioInt.nbCoeffsMFCC; i++)
        ligne += "," + String(audioInt.mfcc[i], 3);
    sd.ecrireLigne(SD_LOG_MFCC_INT, ligne);
}

void _tacheMicExterieur() {
    if (!mic.isReady()) return;
    Serial.println(F("[MIC-EXT] Capture..."));
    audioExt = mic.analyserExterieur(MIC_EXT_CAPTURE_MS,
                                      MIC_EXT_FREQ_MIN_HZ,
                                      MIC_EXT_FREQ_MAX_HZ,
                                      MFCC_NUM_COEFFS);
    if (!audioExt.valide) { Serial.println(F("[MIC-EXT] Invalide.")); return; }
    Serial.printf("[MIC-EXT] Freq: %.1f Hz  RMS: %.4f\n",
                  audioExt.freqDominante, audioExt.energieRMS);

    char ts[20]; rtc.formaterISO(ts);
    String ligne = String(ts) + "," + String(audioExt.freqDominante, 1)
                 + "," + String(audioExt.energieRMS, 4);
    for (int i = 0; i < audioExt.nbCoeffsMFCC; i++)
        ligne += "," + String(audioExt.mfcc[i], 3);
    sd.ecrireLigne(SD_LOG_MFCC_EXT, ligne);
}

void _tacheLoRa() {
    if (!lora.isReady()) return;

    seqLoRa++;

    // ── Remplissage de la trame binaire ──
    TrameBeetter trame;
    Beetter::remplir(trame,
        BEETTER_HIVE_ID,
        rtc.isReady() ? rtc.timestamp() : (uint32_t)(millis() / 1000),
        seqLoRa,
        mesureInt.temperatureC, mesureInt.humiditeRH,
        mesureExt.temperatureC, mesureExt.humiditeRH,
        luminosite,
        audioInt.freqDominante, audioInt.energieRMS,
        audioInt.mfcc, audioInt.nbCoeffsMFCC,
        audioExt.freqDominante, audioExt.energieRMS,
        audioExt.mfcc, audioExt.nbCoeffsMFCC
    );

    // ── Envoi LoRa (83 bytes, CRC calculé automatiquement) ──
    lora.envoyerTrame(trame);

    // ── Notification BLE (affichage debug lisible) ──
    if (wifi.clientBLEConnecte()) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "B:%s S:%u T:%.1f/%.1f F:%.0f/%.0f",
                 BEETTER_HIVE_ID, seqLoRa,
                 mesureInt.temperatureC, mesureExt.temperatureC,
                 audioInt.freqDominante, audioExt.freqDominante);
        wifi.envoyerBLE(String(msg));
        Serial.println(F("[BLE] Notification envoyée."));
    }
}

// ════════════════════════════════════════════════════════════
//  UTILITAIRES
// ════════════════════════════════════════════════════════════

void _initFichiersCSV() {
    if (!SD.exists(SD_LOG_ENV))
        sd.ecrireLigne(SD_LOG_ENV,
            "timestamp,T_int_C,H_int_RH,Ha_int_gm3,T_ext_C,H_ext_RH,Ha_ext_gm3");
    if (!SD.exists(SD_LOG_MFCC_INT)) {
        String ent = "timestamp,freq_dom_Hz,rms";
        for (int i = 0; i < MFCC_NUM_COEFFS; i++) ent += ",mfcc" + String(i);
        sd.ecrireLigne(SD_LOG_MFCC_INT, ent);
    }
    if (!SD.exists(SD_LOG_MFCC_EXT)) {
        String ent = "timestamp,freq_dom_Hz,rms";
        for (int i = 0; i < MFCC_NUM_COEFFS; i++) ent += ",mfcc" + String(i);
        sd.ecrireLigne(SD_LOG_MFCC_EXT, ent);
    }
}

const char* _nomNiveau(NiveauLuminosite n) {
    switch (n) {
        case LUM_NUIT:    return "Nuit";
        case LUM_OMBRE:   return "Ombre";
        case LUM_NUAGEUX: return "Nuageux";
        case LUM_SOLEIL:  return "Soleil";
        default:          return "?";
    }
}
