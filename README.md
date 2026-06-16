# Beetter — Système de surveillance de ruches

Beetter est une plateforme IoT de bout en bout pour la surveillance de ruches. Chaque ruche est équipée de **nœuds capteurs** (un module intérieur et un module extérieur) qui relèvent température, humidité, niveau sonore (fréquence, amplitude et coefficients MFCC) et luminosité, puis transmettent ces mesures sous forme de **trames binaires LoRa**. Un **Raspberry Pi** reçoit les trames, les décode et les stocke dans InfluxDB. Il expose un dashboard web et pousse périodiquement les données vers un **serveur distant** qui agrège plusieurs Raspberry Pi. Une **application Android** interroge ce serveur pour un accès mobile. Un **pipeline ML** tourne séparément pour classifier l'état de la ruche à partir des données MFCC.

```
┌─────────────────────────────────────────────────────────────────────┐
│  Firmware ESP32  (Firmware/)                                        │
│  Nœud intérieur            Nœud extérieur                          │
│  (SHT40 + micro → MFCC)    (SHT40 + micro → MFCC + photorésistance)│
└──────────────────┬──────────────────────────────────────────────────┘
                   │  trames binaires LoRa (blocs ENV + AUD)
                   ▼
        ┌──────────────────────────┐
        │  Raspberry Pi            │
        │  • receiver.py           │──→ InfluxDB (local) [direct]
        │    (Grove LoRa 868 MHz)  │
        │  • Web App Flask :5000   │──→ InfluxDB + PostgreSQL (local)
        │  • Scheduler (push data) │
        └──────────┬───────────────┘
                   │  POST /api/push  (Bearer API key)
                   ▼
        ┌──────────────────────────┐
        │  Serveur distant         │  Flask :5001
        │  • Dashboard agrégé      │──→ InfluxDB + PostgreSQL (distant)
        │  • Gestion des API keys  │
        │  • API REST mobile       │
        └──────────┬───────────────┘
                   │  API REST  (Bearer session token)
                   ▼
            Application Android
```

---

## Capteurs

Chaque ruche porte **deux nœuds capteurs** (intérieur et extérieur). Le microphone de chaque nœud calcule sur le firmware une **analyse MFCC** (13 coefficients) en plus de la fréquence dominante et de l'amplitude.

| Emplacement | Capteur | Grandeur mesurée | Unité |
|---|---|---|---|
| Intérieur | SHT40 | Température intérieure | °C |
| Intérieur | SHT40 | Humidité intérieure | % |
| Intérieur | Microphone | Fréquence dominante intérieure | Hz |
| Intérieur | Microphone | Amplitude intérieure | (relative) |
| Intérieur | Microphone | Coefficients MFCC intérieurs c0…c12 | 13 valeurs |
| Extérieur | SHT40 | Température extérieure | °C |
| Extérieur | SHT40 | Humidité extérieure | % |
| Extérieur | Microphone | Fréquence dominante extérieure | Hz |
| Extérieur | Microphone | Amplitude extérieure | (relative) |
| Extérieur | Microphone | Coefficients MFCC extérieurs c0…c12 | 13 valeurs |
| Extérieur | Photorésistance | Luminosité | ADC |

Par ruche : **9 mesures scalaires** + **26 coefficients MFCC** = **35 mesures** au total.

> **Pourquoi le MFCC ?** La fréquence dominante et l'amplitude donnent une vue grossière du son de la ruche. Les coefficients MFCC (Mel-Frequency Cepstral Coefficients) capturent la texture spectrale complète du bourdonnement, indispensable pour entraîner le modèle ML à distinguer les états de la colonie (essaimage, absence de reine, attaque, etc.).

> **Note** : le projet ne gère **ni CO₂ ni poids**. Ces grandeurs ne font pas partie du matériel et ne doivent apparaître ni dans le code ni dans la documentation.

---

## Modèle de données et formats d'échange

### Noms canoniques des mesures

Identiques de bout en bout (trame LoRa ↔ InfluxDB ↔ API ↔ mobile) :

| Grandeur | Mesure InfluxDB / champ API |
|---|---|
| Température intérieure | `temperature_int` |
| Humidité intérieure | `humidity_int` |
| Température extérieure | `temperature_ext` |
| Humidité extérieure | `humidity_ext` |
| Fréquence dominante intérieure | `sound_freq_int` |
| Amplitude intérieure | `sound_amp_int` |
| Fréquence dominante extérieure | `sound_freq_ext` |
| Amplitude extérieure | `sound_amp_ext` |
| Luminosité extérieure | `light_ext` |
| Coefficients MFCC intérieurs | `mfcc_int_0` … `mfcc_int_12` |
| Coefficients MFCC extérieurs | `mfcc_ext_0` … `mfcc_ext_12` |

La liste complète vit dans `app/blueprints/utils/influxdb.py` (`MEASUREMENTS`, 35 éléments).

---

### 1. Trames LoRa (firmware → récepteur)

Le firmware ESP32 émet des **trames binaires little-endian** avec CRC-16/CCITT. Un paquet LoRa peut contenir un ou plusieurs blocs concaténés. Le récepteur les parse séquentiellement en détectant le magic byte.

**Bloc ENV** — 23 bytes — magic `0xE0` — données environnementales :

```
offset  taille  type      contenu
     0       1  uint8     magic 0xE0
     1       2  uint16    numéro de séquence
     3       4  char[4]   beehive_id (ASCII, ex. "B001")
     7       4  uint32    horodatage Unix (secondes UTC)
    11       2  int16     température intérieure × 100 (°C)
    13       2  uint16    humidité intérieure × 10 (%)
    15       2  int16     température extérieure × 100 (°C)
    17       2  uint16    humidité extérieure × 10 (%)
    19       2  uint16    luminosité ADC
    21       2  uint16    CRC-16/CCITT (sur les 21 premiers bytes)
```

**Bloc AUD** — 73 bytes — magic `0xA0` — données audio et MFCC :

```
offset  taille  type       contenu
     0       1  uint8      magic 0xA0
     1       2  uint16     numéro de séquence
     3       4  char[4]    beehive_id (ASCII)
     7       4  uint32     horodatage Unix (secondes UTC)
    11       2  uint16     sound_freq_int × 10 (Hz)
    13       2  uint16     sound_amp_int × 10000
    15       2  uint16     sound_freq_ext × 10 (Hz)
    17       2  uint16     sound_amp_ext × 10000
    19      26  int16[13]  mfcc_int[0..12] × 100
    45      26  int16[13]  mfcc_ext[0..12] × 100
    71       2  uint16     CRC-16/CCITT
```

---

### 2. Réception et envoi vers l'API Flask

Le récepteur (`lora/receiver.py`) décode les blocs et envoie un relevé JSON à l'API Flask locale (`POST /api/data`), exactement comme `tools/simulate.py`. C'est Flask qui écrit ensuite dans InfluxDB, vérifie les seuils et crée les alertes — le récepteur ne touche plus InfluxDB directement.

> **Point ouvert** : dans les trames binaires, `beehive_id` est un identifiant ASCII 4 chars (ex. `"B001"`). Dans la base PostgreSQL et dans Flask, les ruches ont un identifiant entier. Le récepteur convertit `"B001"` → `1` en extrayant les chiffres de la chaîne (configurable via `BEEHIVE_ID_FALLBACK` en l'absence de chiffre exploitable) ; cette correspondance reste approximative et devra être formalisée si la nomenclature des ruches évolue.

---

### 3. Ingest via l'API Flask (`POST /api/data`)

Utilisé par le simulateur (`tools/simulate.py`) et tout émetteur qui envoie du JSON. Tous les champs capteurs sont **optionnels** — seuls ceux présents sont écrits dans InfluxDB.

```json
{
  "beehive_id": 1,
  "temperature_int": 34.7, "humidity_int": 62.1,
  "temperature_ext": 18.3, "humidity_ext": 55.0,
  "sound_freq_int": 245.0, "sound_amp_int": 0.0042,
  "sound_freq_ext": 120.0, "sound_amp_ext": 0.0011,
  "light_ext": 760.0,
  "mfcc_int": [1.2, -0.5, 0.3, 0.7, -1.1, 0.4, 0.0, -0.2, 0.9, 0.1, -0.3, 0.6, -0.8],
  "mfcc_ext": [0.8, -1.1, 0.6, 0.2, -0.4, 0.5, 0.1, -0.7, 1.0, 0.3, -0.1, 0.4, -0.6],
  "timestamp": "2026-05-19T14:32:00Z"
}
```

`mfcc_int` et `mfcc_ext` sont des listes de **exactement 13 floats** — si la liste est absente ou de longueur incorrecte, les coefficients MFCC ne sont pas écrits (le reste du relevé est traité normalement). `timestamp` est optionnel (défaut : heure de réception).

Réponse : `201 {"status": "ok"}`.

Après écriture, la route vérifie les seuils et crée une alerte automatique si une valeur franchit un seuil critique ou de warning.

---

### 4. Stockage InfluxDB

Une **mesure** (`measurement`) par grandeur, un tag `beehive_id`, un champ `value` :

```
measurement : temperature_int   tag: beehive_id="1"   field: value=34.7
measurement : sound_freq_int    tag: beehive_id="1"   field: value=245.0
measurement : mfcc_int_0        tag: beehive_id="1"   field: value=1.20
measurement : mfcc_int_3        tag: beehive_id="1"   field: value=-2.45
measurement : mfcc_ext_0        tag: beehive_id="1"   field: value=0.80
```

Les 26 mesures MFCC sont stockées mais **exclues des graphes du dashboard** (`CHART_MEASUREMENTS`). Elles sont en revanche incluses dans les exports CSV et utilisées par le pipeline ML.

Bucket : `INFLUXDB_BUCKET` (défaut `sensors`). Organisation : `INFLUXDB_ORG` (défaut `beetter`).

---

### 5. Push vers le serveur distant (`POST /api/push`)

L'application web envoie un lot des données récentes de toutes les ruches. Les champs MFCC ne sont **pas inclus** dans le push (seules les mesures scalaires sont agrégées côté serveur).

```json
{
  "source": "beetter",
  "pushed_at": "2026-05-19T14:35:00Z",
  "beehives": [
    {
      "id": 1,
      "name": "Ruche du verger",
      "location": "12 rue des Tilleuls, Noisy-le-Grand",
      "data": [
        {
          "timestamp": "2026-05-19T14:30:00Z",
          "temperature_int": 34.7, "humidity_int": 62.1,
          "sound_freq_int": 245.0, "light_ext": 760.0
        }
      ]
    }
  ]
}
```

---

### 6. API mobile (serveur → Android)

`GET /api/beehives` — dernières valeurs par mesure scalaire :

```json
{ "beehives": [ { "id": "1", "latest": {
  "temperature_int": {"value": 34.7, "time": "..."},
  "humidity_int":    {"value": 62.1, "time": "..."},
  "sound_freq_int":  {"value": 245.0, "time": "..."},
  "light_ext":       {"value": 760.0, "time": "..."}
} } ] }
```

`GET /api/beehives/<id>/data?range=24h` retourne une série (`labels` + `data`) par mesure scalaire.

---

## Composants

### 1. Firmware (`Firmware/`)

Le firmware embarqué sur les nœuds capteurs ESP32. Il acquiert les données des capteurs, calcule le MFCC sur le microcontrôleur et émet des trames LoRa binaires.

**Stack** : Arduino / C++, bibliothèques maison regroupées dans `src/libraries/` :

| Bibliothèque | Rôle |
|---|---|
| `BeetterMic` | Acquisition microphone, calcul MFCC 13 coefficients |
| `BeetterSHT40` | Lecture capteur température/humidité SHT40 (I²C) |
| `BeetterPhoto` | Lecture photorésistance (ADC) |
| `BeetterLoRa` | Sérialisation et émission des blocs ENV / AUD |
| `BeetterRTC` | Horodatage (RTC) |
| `BeetterSD` | Journalisation locale sur carte SD (optionnel) |

Le code principal est dans `Firmware/src/` (ou `Firmware/Beetter_Main/`). La configuration (fréquence LoRa, spreading factor, identifiant de ruche…) est centralisée dans `BeetterConfig.h`.

---

### 2. Application web (`app/`)

L'application Flask qui tourne sur le Raspberry Pi. Elle reçoit les relevés via son API REST, les stocke dans InfluxDB (time-series) et PostgreSQL (entités), et fournit un dashboard web. Un scheduler pousse périodiquement les données vers les serveurs distants configurés.

**Stack** : Flask 3, PostgreSQL (utilisateurs / ruches / alertes / calendrier), InfluxDB 2 (35 mesures time-series), APScheduler, Gunicorn.

**Blueprints** :

| Blueprint | URL prefix | Rôle |
|---|---|---|
| `api/` | `/api` | Ingest JSON (`POST /api/data`) + données de graphe |
| `auth/` | `/auth` | Connexion, inscription, déconnexion |
| `account/` | `/account` | Gestion du compte et préférences utilisateur |
| `admin/` | `/admin` | Administration des utilisateurs (admin seulement) |
| `beehives/` | `/beehives` | CRUD des ruches (nom, localisation, config LoRa) |
| `dashboard/` | `/dashboard` | Page d'accueil et vue d'ensemble |
| `alerts/` | `/alerts` | Journal des alertes (changements de statut) |
| `settings/` | `/settings` | Configuration des serveurs distants de push |
| `calendar/` | `/calendar` | Calendrier des événements de ruche (inspections, traitements…) |
| `export/` | `/export` | Export CSV de données brutes |
| `setup/` | `/setup` | Assistant de première configuration |
| `utils/` | — | Helpers transverses : InfluxDB, push, seuils, statut, géocodage |

**Modèles PostgreSQL** (`models.py`) :

| Modèle | Rôle |
|---|---|
| `User` | Compte utilisateur (username, email, rôle : viewer / editor / admin) |
| `Beehive` | Ruche (nom, adresse, config LoRa, statut, coordonnées GPS) |
| `Alert` | Journal des changements de statut (source : seuil auto ou manuel) |
| `RemoteServerConfig` | Config de push vers un serveur distant (URL, API key, intervalle) |
| `HiveEvent` | Événement de calendrier lié à une ruche |
| `UserHiveIndicator` | Indicateurs affichés par utilisateur par ruche (préférences dashboard) |
| `UserPreferences` | Préférences utilisateur (langue, unité, notifications, accessibilité) |

**Pipeline de données** : un relevé arrive sur `POST /api/data` → la route vérifie la ruche (PostgreSQL) → écrit les 9 scalaires + jusqu'à 26 MFCC dans InfluxDB → vérifie les seuils et crée une alerte si nécessaire → le dashboard lit InfluxDB pour tracer les graphes scalaires → toutes les minutes, le scheduler pousse les données récentes vers les serveurs distants.

**Variables d'environnement** (`app/.env.example`) :

| Variable | Description |
|---|---|
| `SECRET_KEY` | Clé secrète Flask (longue chaîne aléatoire) |
| `DATABASE_URL` | Chaîne de connexion PostgreSQL |
| `INFLUXDB_URL` | URL de base InfluxDB |
| `INFLUXDB_TOKEN` | Token d'authentification InfluxDB |
| `INFLUXDB_ORG` | Organisation InfluxDB (défaut `beetter`) |
| `INFLUXDB_BUCKET` | Bucket InfluxDB (défaut `sensors`) |

---

### 3. Récepteur LoRa (`lora/`)

Le script Python qui tourne sur le Raspberry Pi. Il écoute les trames binaires des nœuds ESP32 via un module **Grove LoRa 868 MHz** connecté en série (`/dev/serial0`), les décode (blocs ENV et AUD) et envoie chaque relevé à l'API Flask locale (`POST /api/data`).

**Trames supportées** :
- `ENV` (23 bytes, magic `0xE0`) — température, humidité, luminosité
- `AUD` (73 bytes, magic `0xA0`) — fréquence, amplitude, 13 MFCC × 2 micros

Le récepteur gère la **détection de pertes de trames** (comparaison des numéros de séquence par ruche) et journalise les statistiques (reçues / ENV / AUD / erreurs).

**Variables d'environnement** :

| Variable | Défaut | Description |
|---|---|---|
| `API_ENABLE` | `0` | Activer l'envoi vers l'API Flask (`1` pour activer) |
| `BEETTER_API_URL` | `http://localhost:5000` | URL de base de l'app Flask (`app/`) |
| `BEETTER_API_TIMEOUT` | `5` | Timeout (secondes) des requêtes HTTP vers l'API |
| `BEEHIVE_ID_FALLBACK` | `1` | ID de ruche utilisé si `beehive_id` ASCII ne contient aucun chiffre exploitable |

**Lancement** :

```bash
cd lora
pip install -r requirements.txt

# Sans envoi à l'API (affichage console seulement)
python3 receiver.py

# Avec envoi vers l'API Flask — charger les variables du .env à la racine
source ../.env
API_ENABLE=1 python3 receiver.py

# Ou directement avec les variables
API_ENABLE=1 \
  BEETTER_API_URL=http://localhost:5000 \
  python3 receiver.py
```

**Dépendance** : `grove_lora.py` doit être dans le même dossier (pilote série du module Grove LoRa). L'app Flask (`app/`) doit être lancée et accessible à `BEETTER_API_URL` pour que l'envoi fonctionne.

---

### 4. Pipeline ML (`IA/`)

Un pipeline d'apprentissage automatique pour classifier l'état de la ruche à partir des mesures capteurs, en particulier les coefficients MFCC.

**Stack** : Python, PyTorch, influxdb-client.

**États de ruche classifiés** (6 classes) :

| Label | État |
|---|---|
| 0 | `normal` — colonie saine |
| 1 | `pre_swarming` — signes précurseurs d'essaimage |
| 2 | `swarming` — essaimage en cours |
| 3 | `queen_competition` — compétition de reines |
| 4 | `queenless` — absence de reine |
| 5 | `attack` — attaque (frelon, etc.) |

**Vecteur de features** — 17 dimensions par capteur (intérieur ET extérieur) :

```text
[0]    température (°C, z-scorée)
[1]    humidité (%, z-scorée)
[2]    fréquence dominante (Hz, z-scorée)
[3]    amplitude (z-scorée)
[4-16] MFCC c0…c12 (chacun z-scoré individuellement)
```

**Architecture du modèle** — apprentissage contrastif en deux phases :

- **Phase 1 — pré-entraînement non supervisé** : deux encodeurs MLP indépendants (intérieur et extérieur) sont entraînés par paires via une perte InfoNCE. Aucun label requis — seule la cohérence temporelle intérieur/extérieur sert de signal de supervision.
- **Phase 2 — fine-tuning supervisé** : les encodeurs sont gelés et une tête de classification est entraînée sur les données labelisées (~30 événements × 10 paquets par classe).

**Scripts principaux** :

```bash
# Extraire les données d'entraînement depuis InfluxDB
python IA/collect_training_data.py --hive 1 --range 30d --out data/hive1.csv

# Pré-entraîner les encodeurs (non supervisé)
python IA/train_phase1.py

# Fine-tuner le classifieur (supervisé)
python IA/train_phase2.py
```

**Inférence** : une alerte `pre_swarming` est déclenchée lorsque P(pre_swarming) > 0,40 sur N = 3 relevés consécutifs (≈ 15 minutes à un intervalle de 5 minutes), pour éviter les faux positifs.

---

### 5. Serveur distant (`server/`)

Le serveur d'agrégation centralisé. Les Raspberry Pi lui poussent des lots de données scalaires ; l'application Android l'interroge pour afficher les dashboards.

**Stack** : Flask 3, PostgreSQL (utilisateurs / API keys / sessions), InfluxDB 2, Gunicorn.

**Fonctionnalités** :

- Dashboard web affichant toutes les ruches de tous les Raspberry Pi connectés
- Gestion des API keys (génération / révocation)
- API REST mobile avec authentification par token de session (expiration 30 jours)
- Réception des données poussées via `POST /api/push` (Bearer API key)

**Endpoints REST** :

| Méthode | Chemin | Auth | Description |
|---|---|---|---|
| `POST` | `/api/auth/login` | — | Connexion mobile → token de session |
| `POST` | `/api/auth/logout` | Bearer token | Invalide la session |
| `GET` | `/api/beehives` | Bearer token / API key | Liste les ruches avec dernières valeurs |
| `GET` | `/api/beehives/<id>/data` | Bearer token / API key | Données graphe (`?range=24h`) |
| `POST` | `/api/push` | Bearer API key | Réception d'un lot depuis le Raspberry Pi |

**Variables d'environnement** (`server/.env.example`) :

| Variable | Description |
|---|---|
| `SECRET_KEY` | Clé secrète Flask (**différente** de celle de l'app web) |
| `DATABASE_URL` | Chaîne de connexion PostgreSQL |
| `INFLUXDB_URL` | URL InfluxDB |
| `INFLUXDB_TOKEN` | Token InfluxDB |
| `INFLUXDB_ORG` | Organisation InfluxDB |
| `INFLUXDB_BUCKET` | Bucket InfluxDB |

---

### 6. Application Android (`android/`)

Application Android native qui se connecte au serveur distant et affiche les données des ruches en temps réel.

**Stack** : Kotlin, Jetpack Compose, Retrofit 2 + OkHttp, Gson, DataStore, WorkManager.

**Architecture** : MVVM — `data/` (Retrofit, modèles, DataStore, repository), `ui/` (écrans Compose + ViewModels), `worker/` (alertes en arrière-plan via WorkManager).

**Fonctionnalités** :

- Connexion avec l'URL du serveur, nom d'utilisateur et mot de passe (token stocké via DataStore)
- Dashboard listant toutes les ruches avec les dernières valeurs de température et humidité intérieures
- Écran de détail d'une ruche : graphe par mesure scalaire (temp/hum intérieur et extérieur, son fréquence + amplitude des deux micros, luminosité), plage de temps sélectionnable
- Alertes en arrière-plan via WorkManager (seuils min/max sur les valeurs intérieures)
- Paramètres : seuils de notification, déconnexion

**Prérequis** : Android 8.0 (API 26) ou supérieur.

---

### 7. Simulateur (`tools/simulate.py`)

Outil de développement qui envoie de faux relevés réalistes (dérive sinusoïdale + bruit gaussien) via `POST /api/data`, en remplacement du matériel LoRa. C'est l'approche de référence pour envoyer des données à l'application Flask (avec MFCC simulés).

> La ruche doit déjà exister dans la base de données (crée-la via l'interface d'abord).

```bash
cd tools
pip install requests

# Mode live — un relevé toutes les 10 secondes, ruche ID 1
python simulate.py

# Plusieurs ruches
python simulate.py --ids 1 2 3

# Intervalle et URL personnalisés
python simulate.py --url http://raspberrypi.local:5000 --interval 5

# Mode burst — remplit 200 points historiques sur les 24 dernières heures
python simulate.py --burst 200
```

---

## Déploiement

### Application web — Raspberry Pi

**Prérequis** : Docker et Docker Compose.

1. Copie et configure le fichier d'environnement :
   ```bash
   cd app
   cp .env.example .env
   nano .env   # au minimum : SECRET_KEY et INFLUXDB_TOKEN
   ```

2. Build et démarrage :
   ```bash
   docker compose up -d --build
   ```

3. Ouvre `http://<ip-du-raspberry-pi>:5000`. Le premier compte inscrit reçoit automatiquement le rôle **admin**.

4. Démarre le récepteur LoRa (hors Docker, directement sur le Pi) :
   ```bash
   cd lora
   pip install -r requirements.txt
   API_ENABLE=1 BEETTER_API_URL=http://localhost:5000 python3 receiver.py
   ```

**Lancer le récepteur LoRa en tant que service systemd** :

Crée `/etc/systemd/system/beetter-lora.service` :

```ini
[Unit]
Description=Beetter LoRa Receiver
After=network.target

[Service]
WorkingDirectory=/home/pi/beetter/lora
ExecStart=/usr/bin/python3 /home/pi/beetter/lora/receiver.py
Environment=API_ENABLE=1
Environment=BEETTER_API_URL=http://localhost:5000
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now beetter-lora
```

### Mode kiosk au démarrage — Raspberry Pi (optionnel)

Pour que le Raspberry Pi ouvre automatiquement le dashboard en plein écran.

**1. Script de lancement** `~/beetter-stack/start-dashboard.sh` :

```bash
#!/bin/bash
until curl -s -o /dev/null http://localhost:5000; do
    sleep 1
done
firefox --kiosk http://localhost:5000
```

```bash
chmod +x ~/beetter-stack/start-dashboard.sh
```

**2. Entrée d'autostart** `~/.config/autostart/beetter-dashboard.desktop` :

```ini
[Desktop Entry]
Type=Application
Name=Beetter Dashboard
Exec=/home/<user>/beetter-stack/start-dashboard.sh
X-GNOME-Autostart-enabled=true
```

**3. Activer l'autologin** :

```bash
sudo raspi-config
# System Options → Boot / Auto Login → Desktop Autologin
```

#### Sortir du mode kiosk

| Action | Raccourci |
| --- | --- |
| Fermer Firefox | `Alt+F4` |
| Basculer sur un terminal | `Ctrl+Alt+F2` |
| Revenir à l'interface graphique | `Ctrl+Alt+F1` |

Pour désactiver temporairement :

```bash
mv ~/.config/autostart/beetter-dashboard.desktop \
   ~/.config/autostart/beetter-dashboard.desktop.disabled
```

---

### Serveur distant

**Prérequis** : serveur Linux avec Docker et Docker Compose.

1. ```bash
   cd server
   cp .env.example .env
   nano .env   # SECRET_KEY différent de l'app web
   ```

2. ```bash
   docker compose up -d --build
   ```

3. Ouvre `http://<ip-du-serveur>:5001`. Inscris un compte (le premier est admin).

4. Génère une API key : **Dashboard → API Keys → New key**.

---

### Connexion de l'application web au serveur distant

1. Ouvre `http://<ip-du-raspberry-pi>:5000` et connecte-toi.
2. Va dans **Paramètres → Ajouter un serveur**.
3. Renseigne l'URL du serveur, l'API key et l'intervalle de push (en minutes).
4. Sauvegarde. Le badge de statut passe au **vert** au premier push réussi.

---

### Application Android

#### Build

**Prérequis** : Android Studio Hedgehog (2023.1) ou ultérieur, ou JDK 17+.

**Depuis Android Studio** :
1. Ouvre le dossier `android/` comme projet.
2. Attends la synchronisation Gradle.
3. **Build → Build Bundle(s) / APK(s) → Build APK(s)**.

**Depuis la ligne de commande** :

```bash
cd android
./gradlew assembleDebug          # Linux / macOS
gradlew.bat assembleDebug        # Windows
```

#### Installation

```bash
adb install android/app/build/outputs/apk/debug/app-debug.apk
```

#### Premier lancement

1. Entre l'URL du serveur distant — ex. `http://192.168.1.10:5001`.
2. Entre le nom d'utilisateur et le mot de passe.
3. Appuie sur **Se connecter**.

---

## Développement — démarrage rapide (sans Docker)

```bash
# Application web
cd app
python -m venv .venv
source .venv/bin/activate        # Windows: .venv\Scripts\activate
pip install -r requirements.txt
cp .env.example .env             # édite DATABASE_URL / INFLUXDB_*
flask run --debug

# Remplir les graphes avec de fausses données (crée d'abord une ruche dans l'interface)
cd tools
python simulate.py --burst 200

# Serveur distant
cd server
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
flask run --debug --port 5001
```

---

## Structure du projet

```
beetter/
├── app/                   # Application web Raspberry Pi (Flask :5000)
│   ├── blueprints/
│   │   ├── api/           # POST /api/data — ingest JSON + données de graphe
│   │   ├── auth/          # Connexion / inscription / déconnexion
│   │   ├── account/       # Gestion du compte et préférences utilisateur
│   │   ├── admin/         # Administration des utilisateurs
│   │   ├── beehives/      # CRUD des ruches
│   │   ├── dashboard/     # Page d'accueil
│   │   ├── alerts/        # Journal des alertes
│   │   ├── settings/      # Configuration des serveurs distants
│   │   ├── calendar/      # Calendrier des événements de ruche
│   │   ├── export/        # Export CSV
│   │   ├── setup/         # Assistant de première configuration
│   │   └── utils/         # Helpers : InfluxDB (35 mesures), seuils, statut, push
│   ├── templates/
│   ├── static/
│   ├── models.py          # Modèles SQLAlchemy (User, Beehive, Alert,
│   │                      #   RemoteServerConfig, HiveEvent,
│   │                      #   UserHiveIndicator, UserPreferences)
│   ├── scheduler.py       # Job APScheduler de push
│   ├── migrate.py         # Création / mise à jour des tables
│   ├── compose.yml
│   └── Dockerfile
├── server/                # Serveur d'agrégation distant (Flask :5001)
│   ├── blueprints/
│   │   ├── api/           # API REST mobile & push
│   │   ├── auth/
│   │   ├── dashboard/
│   │   └── utils/         # Helpers InfluxDB
│   ├── models.py          # User, ApiKey, UserSession
│   ├── compose.yml
│   └── Dockerfile
├── Firmware/              # Firmware ESP32 (Arduino / C++)
│   ├── src/
│   │   ├── BeetterConfig.h              # Configuration (fréquence, SF, ID ruche…)
│   │   └── libraries/
│   │       ├── BeetterMic/              # Acquisition micro + MFCC 13 coefficients
│   │       ├── BeetterSHT40/            # Capteur température / humidité SHT40
│   │       ├── BeetterPhoto/            # Photorésistance (ADC)
│   │       ├── BeetterLoRa/             # Sérialisation et émission blocs ENV / AUD
│   │       ├── BeetterRTC/              # Horodatage RTC
│   │       └── BeetterSD/               # Journalisation SD (optionnel)
│   └── README.md
├── IA/                    # Pipeline ML (classification d'état de ruche)
│   ├── beehive/
│   │   ├── data.py        # Décodage des trames + feature engineering (17-d par capteur)
│   │   ├── model.py       # Encodeurs MLP + tête de classification (6 classes)
│   │   └── config.py      # Hyperparamètres (batch, lr, température InfoNCE…)
│   ├── collect_training_data.py  # Export CSV depuis InfluxDB
│   ├── train_phase1.py    # Pré-entraînement contrastif non supervisé
│   └── train_phase2.py    # Fine-tuning supervisé
├── lora/                  # Récepteur LoRa (Grove 868 MHz via série)
│   ├── receiver.py        # Décode blocs ENV / AUD, écrit dans InfluxDB
│   ├── grove_lora.py      # Pilote série du module Grove LoRa
│   └── requirements.txt
├── android/               # Application Android (Kotlin + Jetpack Compose)
│   └── app/src/main/java/fr/esiee/beetter/
│       ├── data/          # Retrofit, modèles, DataStore, repository
│       ├── ui/            # Écrans et ViewModels
│       └── worker/        # Worker d'alertes (WorkManager)
└── tools/
    └── simulate.py        # Simulateur de données capteurs (avec MFCC simulés)
```
