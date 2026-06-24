/**
 * =============================================================
 *  Beetter_Main.ino  –  Programme principal Beetter Hive
 * =============================================================
 *  Cycle séquentiel toutes les 30s :
 *  1. SHT40 INT + EXT
 *  2. Photo (indice 0.0–10.0 via loi puissance GL5539)
 *  3. MFCC INT (3s)
 *  4. MFCC EXT (3s)
 *  5. Écriture SD (3 fichiers ouverts une seule fois par cycle)
 *  6. Envoi LoRa (blocs 0xE0 ENV + 0xA0 AUD = 100 bytes)
 *  7. Attente duty cycle
 *
 *  Optimisations appliquées :
 *  - SD : fichiers ouverts/fermés une seule fois par cycle (beginCycle/endCycle)
 *  - CSV : snprintf() statique, pas de String dynamique
 *  - seqLoRa : uint32_t (pas d'overflow à 65535 cycles)
 *  - WiFi : déconnecté après sync NTP (~40 mA économisés)
 *  - Buffer capture statique 94KB (plus de malloc → plus de fragmentation heap)
 * =============================================================
 */

#include "BeetterConfig.h"
#include "esp_task_wdt.h"   // Watchdog matériel ESP32
#include "src/BeetterSD.h"
#include "src/BeetterSHT40.h"
#include "src/BeetterMic.h"
#include "src/BeetterRTC.h"
#include "src/BeetterLoRa.h"
#include "src/BeetterPhoto.h"
#include "src/BeetterWifi.h"
#include "src/BeetterWAV.h"

BeetterSD     sd;
BeetterSHT40  sht;
BeetterMic    mic;
BeetterRTC    rtc;
BeetterLoRa   lora;
BeetterPhoto  photo;
BeetterWifi   wifi;
BeetterWAV    wav;

bool     sdPrete = false;
uint32_t seqLoRa = 0;   // uint32 : pas d'overflow avant ~4 milliards de cycles

// Précalculé une fois – évite un log10f() à chaque cycle
static const float LOG10_LUX_MAX_P1 = log10f(1.0f + LUX_MAX);

// Buffer CSV statique – évite les allocations String à chaque cycle
static char _csvBuf[512];

void _initFichiersCSV();

// ════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    neopixelWrite(LED_RGB_PIN, 0, 0, 0);

    // Watchdog : redémarre automatiquement si le programme se bloque > WDT_TIMEOUT_SEC
    esp_task_wdt_config_t wdt_cfg = { .timeout_ms = WDT_TIMEOUT_SEC * 1000,
                                      .idle_core_mask = 0, .trigger_panic = true };
    esp_task_wdt_reconfigure(&wdt_cfg);
    esp_task_wdt_add(NULL);  // surveiller la tâche courante (loop)
    Serial.printf("[WDT] Watchdog actif – timeout %ds\n", WDT_TIMEOUT_SEC);

    Serial.println(F("\n╔════════════════════════════════╗"));
    Serial.println(F("║    BEETTER HIVE – Démarrage    ║"));
    Serial.printf (  "║    Ruche : %-20s║\n", BEETTER_HIVE_ID);
    Serial.println(F("╚════════════════════════════════╝\n"));

    // ── RTC AVANT la SD ───────────────────────────────────────
    // L'horloge système doit être calée sur le RTC avant toute écriture
    // SD, sinon les fichiers sont datés au plancher FAT (1980-01-01).
    Wire.begin(I2C0_SDA, I2C0_SCL);
    Wire.setClock(100000);   // 100 kHz : marge de bruit accrue (bus partagé, milieu humide)
    if (rtc.begin()) rtc.synchroniserHorlogeSysteme();
    else             Serial.println(F("[WARN] RTC absent."));

    sdPrete = sd.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS, SD_DET);
    if (!sdPrete) Serial.println(F("[WARN] SD non disponible."));
    else { sd.listerRacine(); _initFichiersCSV(); }

    if (!sht.begin(I2C0_SDA, I2C0_SCL, SW_SDA, SW_SCL))
        Serial.println(F("[WARN] Aucun SHT40 trouvé."));

    uint32_t fsMax = max(MIC_INT_SAMPLE_RATE_HZ, MIC_EXT_SAMPLE_RATE_HZ);
    if (!mic.begin(I2S_BCLK, I2S_WS, I2S_DIN, fsMax))
        Serial.println(F("[WARN] Micros I2S non disponibles."));

    photo.begin(PHOTO_PIN, 12, 4);

    // WiFi utilisé uniquement pour la sync NTP au démarrage
    // Déconnecté ensuite pour économiser ~40 mA en continu
    bool wifiOk = wifi.connecter(WIFI_SSID, WIFI_PASSWORD, 15, 500);
    if (wifiOk && rtc.isReady()) {
        rtc.syncNTP(NTP_SERVER, NTP_GMT_OFFSET_SEC, NTP_DST_OFFSET_SEC);
        wifi.deconnecter();   // libère ~40 mA
        Serial.println(F("[WiFi] Deconnecte apres sync NTP."));
    }

    // BLE désactivé – économie mémoire et consommation (~15 mA)
    // Réactiver si nécessaire : wifi.demarrerBLE(BT_DEVICE_NAME);

    if (!lora.begin(LORA_RX_PIN, LORA_TX_PIN, LORA_FREQ))
        Serial.println(F("[WARN] Module LoRa non disponible."));

    // WAV : dossier /wav/ créé si nécessaire
    if (sdPrete) wav.begin(I2S_BCLK, I2S_WS, I2S_DIN, MIC_INT_SAMPLE_RATE_HZ);

    Serial.println(F("\n[BEETTER] Debut des cycles.\n"));
    neopixelWrite(LED_RGB_PIN, 0, 50, 0);
    delay(500);
    neopixelWrite(LED_RGB_PIN, 0, 0, 0);
}

// ════════════════════════════════════════════════════════════
void loop() {
    esp_task_wdt_reset();  // Signaler que le programme est vivant
    unsigned long debutCycle = millis();
    seqLoRa++;
    Serial.printf("\n══ Cycle #%lu ══\n", (unsigned long)seqLoRa);

    // Recalage périodique (~1 h) de l'horloge système sur le RTC :
    // borne la dérive de l'oscillateur ESP32 → dates FAT restent justes.
    if (rtc.isReady() && (seqLoRa % 120 == 0)) rtc.synchroniserHorlogeSysteme();

    // ── 1. SHT40 ─────────────────────────────────────────────
    MesureSHT40 mesureInt = sht.lireInterieur(0);
    MesureSHT40 mesureExt = sht.lireExterieur(0);
    Serial.printf("[ENV] Int: %.2f°C %.1f%%RH  |  Ext: %.2f°C %.1f%%RH\n",
        mesureInt.temperatureC, mesureInt.humiditeRH,
        mesureExt.temperatureC, mesureExt.humiditeRH);

    // ── 2. Photo → indice 0.0–10.0 ───────────────────────────
    uint16_t lumBrut = photo.lire(0);
    float r_ldr      = R_FIXE_OHM * (float)(4095 - lumBrut) / (float)(lumBrut + 1);
    float lux        = powf(LDR_A / r_ldr, 1.0f / LDR_GAMMA);
    float lumIndice  = log10f(1.0f + lux) / LOG10_LUX_MAX_P1 * 10.0f;
    lumIndice        = constrain(lumIndice, 0.0f, 10.0f);
    Serial.printf("[PHOTO] Brut: %d  Lux: %.1f  Indice: %.1f/10\n",
                  lumBrut, lux, lumIndice);

    // ── 3. MFCC intérieur (3s) ───────────────────────────────
    ResultatAudio audioInt = {0, 0, {0}, 0, false};
    if (mic.isReady()) {
        Serial.println(F("[MIC-INT] Capture 3s..."));
        audioInt = mic.analyserInterieur(MIC_INT_CAPTURE_MS,
                                          MIC_INT_FREQ_MIN_HZ,
                                          MIC_INT_FREQ_MAX_HZ,
                                          MFCC_NUM_COEFFS);
        if (audioInt.valide)
            Serial.printf("[MIC-INT] Freq: %.1f Hz  RMS: %.4f\n",
                          audioInt.freqDominante, audioInt.energieRMS);
        else
            Serial.println(F("[MIC-INT] Invalide."));
    }

    // ── 4. MFCC extérieur (3s) ───────────────────────────────
    // Pas de delay() : buffer statique, pas de malloc/free entre INT et EXT
    ResultatAudio audioExt = {0, 0, {0}, 0, false};
    if (mic.isReady()) {
        Serial.println(F("[MIC-EXT] Capture 3s..."));
        audioExt = mic.analyserExterieur(MIC_EXT_CAPTURE_MS,
                                          MIC_EXT_FREQ_MIN_HZ,
                                          MIC_EXT_FREQ_MAX_HZ,
                                          MFCC_NUM_COEFFS);
        if (audioExt.valide)
            Serial.printf("[MIC-EXT] Freq: %.1f Hz  RMS: %.4f\n",
                          audioExt.freqDominante, audioExt.energieRMS);
        else
            Serial.println(F("[MIC-EXT] Invalide."));
    }

    // ── 5. Écriture SD (un seul open/close par fichier) ───────
    if (sdPrete && sd.beginCycle()) {
        char ts[20];
        rtc.formaterISO(ts);

        // ENV – snprintf() statique, pas de String
        snprintf(_csvBuf, sizeof(_csvBuf),
                 "%s,%.2f,%.1f,%.2f,%.2f,%.1f,%.2f,%u,%.1f",
                 ts,
                 mesureInt.temperatureC, mesureInt.humiditeRH, mesureInt.humiditeAbs,
                 mesureExt.temperatureC, mesureExt.humiditeRH, mesureExt.humiditeAbs,
                 (unsigned)lumBrut, lumIndice);
        sd.ecrireEnv(_csvBuf);

        // MFCC INT
        if (audioInt.valide) {
            int pos = snprintf(_csvBuf, sizeof(_csvBuf),
                               "%s,%.1f,%.4f",
                               ts, audioInt.freqDominante, audioInt.energieRMS);
            for (int i = 0; i < audioInt.nbCoeffsMFCC && pos < (int)sizeof(_csvBuf) - 12; i++)
                pos += snprintf(_csvBuf + pos, sizeof(_csvBuf) - pos,
                                ",%.3f", audioInt.mfcc[i]);
            sd.ecrireMfccInt(_csvBuf);
        }

        // MFCC EXT
        if (audioExt.valide) {
            int pos = snprintf(_csvBuf, sizeof(_csvBuf),
                               "%s,%.1f,%.4f",
                               ts, audioExt.freqDominante, audioExt.energieRMS);
            for (int i = 0; i < audioExt.nbCoeffsMFCC && pos < (int)sizeof(_csvBuf) - 12; i++)
                pos += snprintf(_csvBuf + pos, sizeof(_csvBuf) - pos,
                                ",%.3f", audioExt.mfcc[i]);
            sd.ecrireMfccExt(_csvBuf);
        }

        sd.endCycle();
        Serial.println(F("[SD] Ecrit."));
    }

    // ── 6. Envoi LoRa ────────────────────────────────────────
    if (lora.isReady()) {
        uint32_t ts;
        if (rtc.isReady()) {
            ts = rtc.timestamp();
        } else {
            ts = (uint32_t)(millis() / 1000);
            Serial.println(F("[WARN] RTC non pret – timestamp approximatif (millis)."));
        }

        lora.envoyerTrame(
            BEETTER_HIVE_ID, ts, seqLoRa,
            mesureInt.temperatureC, mesureInt.humiditeRH,
            mesureExt.temperatureC, mesureExt.humiditeRH,
            lumIndice,
            audioInt.freqDominante, audioInt.energieRMS,
            audioInt.mfcc, audioInt.nbCoeffsMFCC,
            audioExt.freqDominante, audioExt.energieRMS,
            audioExt.mfcc, audioExt.nbCoeffsMFCC
        );
    } else {
        Serial.println(F("[LoRa] Module absent – trame ignoree ce cycle."));
    }

    // ── 7. Attente duty cycle ─────────────────────────────────
    unsigned long dureeCycle = millis() - debutCycle;
    long resteMs = (long)CYCLE_INTERVAL_MS - (long)dureeCycle;
    Serial.printf("[CYCLE] Duree: %lu ms | Attente: %ld ms\n",
                  dureeCycle, max(resteMs, 0L));
    // Enregistrement WAV tous les WAV_EVERY_CYCLES cycles
    // Effectué pendant l'attente pour ne pas retarder les mesures
    if (sdPrete && resteMs > (WAV_DUREE_SEC * 1000L + 200L)) {
        wav.enregistrerSiNecessaire(seqLoRa, rtc);
        // Signaler au mic que le bus I2S a été utilisé par WAV
        // → réinit conditionnelle à la prochaine capture (évite delay inutile sinon)
        mic.marquerI2SUtiliseParWAV();
    }

    // Attendre le reste du cycle
    unsigned long elapsed = millis() - debutCycle;
    long resteMs2 = (long)CYCLE_INTERVAL_MS - (long)elapsed;
    if (resteMs2 > 0) delay((unsigned long)resteMs2);
}

// ════════════════════════════════════════════════════════════
void _initFichiersCSV() {
    // Écrit l'en-tête si le fichier est absent OU vide (ex. après rm du contenu)
    auto _needsHeader = [](const char* chemin) -> bool {
        if (!SD.exists(chemin)) return true;
        File f = SD.open(chemin, FILE_READ);
        if (!f) return true;
        bool vide = (f.size() == 0);
        f.close();
        return vide;
    };

    if (_needsHeader(SD_LOG_ENV))
        sd.ecrireLigne(SD_LOG_ENV,
            "timestamp,T_int_C,H_int_RH,Ha_int_gm3,T_ext_C,H_ext_RH,Ha_ext_gm3,lum_brut,lum_indice");

    if (_needsHeader(SD_LOG_MFCC_INT)) {
        snprintf(_csvBuf, sizeof(_csvBuf), "timestamp,freq_dom_Hz,rms");
        for (int i = 0; i < MFCC_NUM_COEFFS; i++) {
            char tmp[10];
            snprintf(tmp, sizeof(tmp), ",mfcc%d", i);
            strncat(_csvBuf, tmp, sizeof(_csvBuf) - strlen(_csvBuf) - 1);
        }
        sd.ecrireLigne(SD_LOG_MFCC_INT, _csvBuf);
    }

    if (_needsHeader(SD_LOG_MFCC_EXT)) {
        snprintf(_csvBuf, sizeof(_csvBuf), "timestamp,freq_dom_Hz,rms");
        for (int i = 0; i < MFCC_NUM_COEFFS; i++) {
            char tmp[10];
            snprintf(tmp, sizeof(tmp), ",mfcc%d", i);
            strncat(_csvBuf, tmp, sizeof(_csvBuf) - strlen(_csvBuf) - 1);
        }
        sd.ecrireLigne(SD_LOG_MFCC_EXT, _csvBuf);
    }
}
