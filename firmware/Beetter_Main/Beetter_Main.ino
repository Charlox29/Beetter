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
 *  6. Envoi LoRa (blocs 0xE0 ENV + 0xA0 AUD = 96 bytes)
 *  7. Attente duty cycle
 *
 *  Optimisations appliquées :
 *  - SD : fichiers ouverts/fermés une seule fois par cycle (beginCycle/endCycle)
 *  - CSV : snprintf() statique, pas de String dynamique
 *  - seqLoRa : uint32_t (pas d'overflow à 65535 cycles)
 *  - WiFi : déconnecté après sync NTP (~40 mA économisés)
 *  - delay(100) et log heap supprimés (buffer statique, plus nécessaires)
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

BeetterSD     sd;
BeetterSHT40  sht;
BeetterMic    mic;
BeetterRTC    rtc;
BeetterLoRa   lora;
BeetterPhoto  photo;
BeetterWifi   wifi;

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

    Serial.println(F("\n╔════════════════════════════════╗"));
    Serial.println(F("║    BEETTER HIVE – Démarrage    ║"));
    Serial.printf (  "║    Ruche : %-20s║\n", BEETTER_HIVE_ID);
    Serial.println(F("╚════════════════════════════════╝\n"));

    sdPrete = sd.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS, SD_DET);
    if (!sdPrete) Serial.println(F("[WARN] SD non disponible."));
    else { sd.listerRacine(); _initFichiersCSV(); }

    Wire.begin(I2C0_SDA, I2C0_SCL);
    if (!rtc.begin()) Serial.println(F("[WARN] RTC absent."));

    if (!sht.begin(I2C0_SDA, I2C0_SCL, SW_SDA, SW_SCL))
        Serial.println(F("[WARN] Aucun SHT40 trouvé."));

    uint32_t fsMax = max(MIC_INT_SAMPLE_RATE_HZ, MIC_EXT_SAMPLE_RATE_HZ);
    if (!mic.begin(I2S_BCLK, I2S_WS, I2S_DIN, fsMax))
        Serial.println(F("[WARN] Micros I2S non disponibles."));

    photo.begin(PHOTO_PIN, 12, 4);

    // WiFi uniquement pour la sync NTP, déconnecté ensuite (fix 7)
    bool wifiOk = wifi.connecter(WIFI_SSID, WIFI_PASSWORD, 15, 500);
    if (wifiOk && rtc.isReady()) {
        rtc.syncNTP(NTP_SERVER, NTP_GMT_OFFSET_SEC, NTP_DST_OFFSET_SEC);
        wifi.deconnecter();   // libère ~40 mA
        Serial.println(F("[WiFi] Deconnecte apres sync NTP."));
    }

    wifi.demarrerBLE(BT_DEVICE_NAME);

    if (!lora.begin(LORA_RX_PIN, LORA_TX_PIN, LORA_FREQ))
        Serial.println(F("[WARN] Module LoRa non disponible."));

    Serial.println(F("\n[BEETTER] Debut des cycles.\n"));
    neopixelWrite(LED_RGB_PIN, 0, 50, 0);
    delay(500);
    neopixelWrite(LED_RGB_PIN, 0, 0, 0);
}

// ════════════════════════════════════════════════════════════
void loop() {
    unsigned long debutCycle = millis();
    seqLoRa++;
    Serial.printf("\n══ Cycle #%lu ══\n", (unsigned long)seqLoRa);

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
        uint32_t ts = rtc.isReady() ? rtc.timestamp() : (uint32_t)(millis() / 1000);

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

        if (wifi.clientBLEConnecte()) {
            snprintf(_csvBuf, sizeof(_csvBuf),
                     "B:%s #%lu T:%.1f/%.1f L:%.1f F:%.0f/%.0f",
                     BEETTER_HIVE_ID, (unsigned long)seqLoRa,
                     mesureInt.temperatureC, mesureExt.temperatureC,
                     lumIndice,
                     audioInt.freqDominante, audioExt.freqDominante);
            wifi.envoyerBLE(String(_csvBuf));
        }
    }

    // ── 7. Attente duty cycle ─────────────────────────────────
    unsigned long dureeCycle = millis() - debutCycle;
    long resteMs = (long)CYCLE_INTERVAL_MS - (long)dureeCycle;
    Serial.printf("[CYCLE] Duree: %lu ms | Attente: %ld ms\n",
                  dureeCycle, max(resteMs, 0L));
    if (resteMs > 0) delay((unsigned long)resteMs);
}

// ════════════════════════════════════════════════════════════
void _initFichiersCSV() {
    if (!SD.exists(SD_LOG_ENV))
        sd.ecrireLigne(SD_LOG_ENV,
            "timestamp,T_int_C,H_int_RH,Ha_int_gm3,T_ext_C,H_ext_RH,Ha_ext_gm3,lum_brut,lum_indice");
    if (!SD.exists(SD_LOG_MFCC_INT)) {
        snprintf(_csvBuf, sizeof(_csvBuf), "timestamp,freq_dom_Hz,rms");
        for (int i = 0; i < MFCC_NUM_COEFFS; i++) {
            char tmp[10];
            snprintf(tmp, sizeof(tmp), ",mfcc%d", i);
            strncat(_csvBuf, tmp, sizeof(_csvBuf) - strlen(_csvBuf) - 1);
        }
        sd.ecrireLigne(SD_LOG_MFCC_INT, _csvBuf);
    }
    if (!SD.exists(SD_LOG_MFCC_EXT)) {
        snprintf(_csvBuf, sizeof(_csvBuf), "timestamp,freq_dom_Hz,rms");
        for (int i = 0; i < MFCC_NUM_COEFFS; i++) {
            char tmp[10];
            snprintf(tmp, sizeof(tmp), ",mfcc%d", i);
            strncat(_csvBuf, tmp, sizeof(_csvBuf) - strlen(_csvBuf) - 1);
        }
        sd.ecrireLigne(SD_LOG_MFCC_EXT, _csvBuf);
    }
}
