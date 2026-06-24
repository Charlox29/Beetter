<div align="center">
  <img src="https://beetter.fr/Logo_Beetter.svg" alt="Beetter" width="220"/>
  <br/><br/>

  [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
  [![Contributing](https://img.shields.io/badge/Contributing-guide-blue.svg)](CONTRIBUTING.md)
  [![Website](https://img.shields.io/badge/Website-beetter.fr-4CAF50.svg)](https://beetter.fr)
</div>

---

# Beetter — Système de surveillance de ruches

Beetter est une plateforme IoT de bout en bout pour la surveillance de ruches. Chaque ruche est équipée de **nœuds capteurs** (un module intérieur et un module extérieur) qui relèvent température, humidité, niveau sonore (fréquence, amplitude et coefficients MFCC) et luminosité, puis transmettent ces mesures sous forme de **trames binaires LoRa**. Un **Raspberry Pi** reçoit les trames, les décode et les stocke dans InfluxDB. Il expose un dashboard web et pousse périodiquement les données vers un **serveur distant** qui agrège plusieurs Raspberry Pi. Une **application mobile Flutter** (Android + iOS) interroge ce serveur pour un accès mobile. Un **pipeline ML** tourne séparément pour classifier l'état de la ruche à partir des données MFCC.

```
┌─────────────────────────────────────────────────────────────────────┐
│  Firmware ESP32  (firmware/)                                        │
│  Nœud intérieur            Nœud extérieur                          │
│  (SHT40 + micro → MFCC)    (SHT40 + micro → MFCC + photorésistance)│
└──────────────────┬──────────────────────────────────────────────────┘
                   │  trames binaires LoRa (blocs ENV + AUD)
                   ▼
        ┌──────────────────────────┐
        │  Raspberry Pi            │
        │  • receiver.py           │──→ InfluxDB (local)
        │    (Grove LoRa 868 MHz)  │
        │  • Web App Flask :5000   │──→ InfluxDB + PostgreSQL (local)
        │  • Scheduler (push data) │
        └──────────┬───────────────┘
                   │  POST /api/push  (Bearer API key)  ← offline-safe
                   ▼
        ┌──────────────────────────┐
        │  Serveur distant         │  Flask :5001
        │  • Dashboard admin       │──→ InfluxDB + PostgreSQL (distant)
        │  • Gestion des API keys  │
        │  • Gestion des utilisateurs│
        │  • API REST mobile (JWT) │
        └──────────┬───────────────┘
                   │  API REST  (Bearer JWT, 30 jours)
                   ▼
        Application mobile Flutter (Android + iOS)
```

> **Authentification centralisée (Option A — JWT)** : le serveur distant est le fournisseur d'identité. Les utilisateurs créent un compte sur le serveur. L'application Pi peut être liée au serveur — les utilisateurs se connectent alors avec leurs identifiants distants, même hors ligne (le compte est mis en cache localement au premier login). Voir [`docs/AUTH.md`](docs/AUTH.md).

## Documentation technique

| Document | Contenu |
|---|---|
| [docs/AUTH.md](docs/AUTH.md) | Architecture d'authentification JWT — pont Pi/serveur, flux mobile, configuration |
| [docs/API.md](docs/API.md) | Référence complète des endpoints REST (app/ et server/) |
| [docs/APP.md](docs/APP.md) | Application Pi — blueprints, modèles, scheduler, pipeline InfluxDB, seuils |
| [docs/SERVER.md](docs/SERVER.md) | Serveur distant — API REST, dashboard admin, JWT, Docker |
| [docs/MOBILE.md](docs/MOBILE.md) | Application mobile Flutter — architecture Riverpod, services, build |
| [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) | Guide de déploiement — Docker, systemd, LoRa, Nginx, sauvegardes |
| [docs/INFRASTRUCTURE.md](docs/INFRASTRUCTURE.md) | Schémas infra, flux de données, schémas BDD, ports, variables d'env |

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
| Extérieur | Photorésistance | Luminosité | indice 0–10 |

Par ruche : **9 mesures scalaires** + **26 coefficients MFCC** = **35 mesures** au total.

> **Pourquoi le MFCC ?** La fréquence dominante et l'amplitude donnent une vue grossière du son de la ruche. Les coefficients MFCC (Mel-Frequency Cepstral Coefficients) capturent la texture spectrale complète du bourdonnement, indispensable pour entraîner le modèle ML à distinguer les états de la colonie (essaimage, absence de reine, attaque, etc.).

---

## Modèle de données

### Noms canoniques des mesures

Identiques de bout en bout (trame LoRa ↔ InfluxDB ↔ API) :

| Grandeur | Champ InfluxDB / API |
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

### Trames LoRa (firmware → récepteur)

Le firmware ESP32 émet des **trames binaires little-endian** avec CRC-16/CCITT. Un paquet peut contenir un ou plusieurs blocs concaténés, détectés par leur magic byte.

**Bloc ENV** — 25 bytes — magic `0xE0` :

```
offset  taille  type      contenu
     0       1  uint8     magic 0xE0
     1       4  uint32    numéro de séquence (pas d'overflow avant ~4 milliards de cycles)
     5       4  char[4]   beehive_id (ASCII, ex. "B001")
     9       4  uint32    horodatage Unix (secondes UTC)
    13       2  int16     température intérieure × 100 (°C)
    15       2  uint16    humidité intérieure × 10 (%)
    17       2  int16     température extérieure × 100 (°C)
    19       2  uint16    humidité extérieure × 10 (%)
    21       2  uint16    luminosité × 10 (indice 0–10)
    23       2  uint16    CRC-16/CCITT (sur les 23 premiers bytes)
```

**Bloc AUD** — 75 bytes — magic `0xA0` :

```
offset  taille  type       contenu
     0       1  uint8      magic 0xA0
     1       4  uint32     numéro de séquence (aligné avec bloc ENV)
     5       4  char[4]    beehive_id (ASCII)
     9       4  uint32     horodatage Unix (secondes UTC)
    13       2  uint16     sound_freq_int × 10 (Hz)
    15       2  uint16     sound_amp_int × 10000
    17       2  uint16     sound_freq_ext × 10 (Hz)
    19       2  uint16     sound_amp_ext × 10000
    21      26  int16[13]  mfcc_int[0..12] — MFCC0 × 10 (résol. 0.1) ; MFCC1..12 × 1000 (résol. 0.001)
    47      26  int16[13]  mfcc_ext[0..12] — MFCC0 × 10 (résol. 0.1) ; MFCC1..12 × 1000 (résol. 0.001)
    73       2  uint16     CRC-16/CCITT
```

### Ingest via l'API Flask (`POST /api/data`)

Le récepteur LoRa et le simulateur envoient tous deux des relevés JSON à cet endpoint. Tous les champs capteurs sont **optionnels** — seuls ceux présents sont écrits dans InfluxDB.

```json
{
  "beehive_id": "B001",
  "temperature_int": 34.7, "humidity_int": 62.1,
  "temperature_ext": 18.3, "humidity_ext": 55.0,
  "sound_freq_int": 245.0, "sound_amp_int": 0.0042,
  "sound_freq_ext": 120.0, "sound_amp_ext": 0.0011,
  "light_ext": 7.6,
  "mfcc_int": [1.2, -0.5, 0.3, 0.7, -1.1, 0.4, 0.0, -0.2, 0.9, 0.1, -0.3, 0.6, -0.8],
  "mfcc_ext": [0.8, -1.1, 0.6, 0.2, -0.4, 0.5, 0.1, -0.7, 1.0, 0.3, -0.1, 0.4, -0.6],
  "timestamp": "2026-05-19T14:32:00Z"
}
```

`mfcc_int` et `mfcc_ext` sont des listes de **exactement 13 floats** ; si absentes ou de longueur incorrecte, les coefficients MFCC sont ignorés (le reste du relevé est traité normalement). `timestamp` est optionnel (défaut : heure de réception). Réponse : `201 {"status": "ok"}`.

---

## Composants

### 1. Firmware (`Firmware/`)

Le firmware embarqué sur les nœuds capteurs ESP32. Il acquiert les données des capteurs, calcule le MFCC sur le microcontrôleur et émet des trames LoRa binaires.

**Stack** : Arduino / C++.

| Bibliothèque | Rôle |
|---|---|
| `BeetterMic` | Acquisition microphone, calcul MFCC 13 coefficients |
| `BeetterSHT40` | Lecture capteur température/humidité SHT40 (I²C) |
| `BeetterPhoto` | Lecture photorésistance (ADC) |
| `BeetterLoRa` | Sérialisation et émission des blocs ENV / AUD |
| `BeetterRTC` | Horodatage RTC |
| `BeetterSD` | Journalisation locale sur carte SD (optionnel) |

La configuration (fréquence LoRa, spreading factor, identifiant de ruche…) est centralisée dans `BeetterConfig.h`.

---

### 2. Application web (`app/`)

L'application Flask qui tourne sur le Raspberry Pi. Elle reçoit les relevés via son API REST, les stocke dans InfluxDB et PostgreSQL, et fournit un dashboard web. Un scheduler pousse périodiquement les données vers les serveurs distants configurés.

**Stack** : Flask 3, PostgreSQL, InfluxDB 2, APScheduler, Gunicorn.

**Blueprints** :

| Blueprint | URL prefix | Rôle |
|---|---|---|
| `api/` | `/api` | Ingest JSON (`POST /api/data`) + données de graphe |
| `auth/` | `/auth` | Connexion, inscription, déconnexion |
| `account/` | `/account` | Gestion du compte et préférences utilisateur |
| `admin/` | `/admin` | Administration des utilisateurs (admin seulement) |
| `beehives/` | `/beehives` | CRUD des ruches |
| `dashboard/` | `/dashboard` | Page d'accueil et vue d'ensemble |
| `alerts/` | `/alerts` | Journal des alertes |
| `settings/` | `/settings` | Configuration des serveurs distants de push |
| `calendar/` | `/calendar` | Calendrier des événements de ruche |
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

**Pipeline de données** : un relevé arrive sur `POST /api/data` → la route vérifie la ruche (PostgreSQL) → écrit les 9 scalaires + jusqu'à 26 MFCC dans InfluxDB → vérifie les seuils et crée une alerte si nécessaire → le dashboard lit InfluxDB pour tracer les graphes scalaires → toutes les minutes, le scheduler pousse les données récentes vers les serveurs distants. Si le serveur est injoignable, `last_push_at` n'est **pas** avancé : les données seront incluses dans le prochain push réussi.

**Authentification** : locale par défaut. Si un serveur distant est configuré dans **Paramètres → Pont de compte distant**, les identifiants inconnus localement sont vérifiés sur le serveur via JWT bridge (Option A). Voir [docs/AUTH.md](docs/AUTH.md).

**Variables d'environnement** (voir `app/.env.example`) :

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

Le script Python qui tourne sur le Raspberry Pi. Il écoute les trames binaires des nœuds ESP32 via un module **Grove LoRa 868 MHz** connecté en série (`/dev/ttyAMA10` sur RPi 5, `/dev/serial0` sur les modèles précédents), les décode (blocs ENV et AUD) et envoie chaque relevé à l'API Flask locale via `POST /api/data`.

Le module Grove RFM95 communique en **UART** avec la bibliothèque RadioHead côté hôte — il n'est pas câblé en SPI direct.

**Trames supportées** :
- `ENV` (25 bytes, magic `0xE0`) — température, humidité, luminosité
- `AUD` (75 bytes, magic `0xA0`) — fréquence, amplitude, 13 MFCC × 2 micros

Le récepteur gère la détection de pertes de trames (numéros de séquence par ruche) et journalise les statistiques (reçues / ENV / AUD / erreurs).

**Variables d'environnement** :

| Variable | Défaut | Description |
|---|---|---|
| `API_ENABLE` | `0` | Activer l'envoi vers l'API Flask (`1` pour activer) |
| `BEETTER_API_URL` | `http://localhost:5000` | URL de base de l'app Flask |
| `BEETTER_API_TIMEOUT` | `5` | Timeout (secondes) des requêtes HTTP |

**Lancement** :

```bash
cd lora
pip install -r requirements.txt

# Sans envoi à l'API (affichage console seulement)
python3 receiver.py

# Avec envoi vers l'API Flask
API_ENABLE=1 BEETTER_API_URL=http://localhost:5000 python3 receiver.py
```

---

### 4. Pipeline ML (`IA/`)

Un pipeline d'apprentissage automatique pour classifier l'état de la ruche à partir des mesures capteurs, en particulier les coefficients MFCC.

**Stack** : Python, PyTorch, influxdb-client.

**États classifiés** (6 classes) :

| Label | État |
|---|---|
| 0 | `normal` — colonie saine |
| 1 | `pre_swarming` — signes précurseurs d'essaimage |
| 2 | `swarming` — essaimage en cours |
| 3 | `queen_competition` — compétition de reines |
| 4 | `queenless` — absence de reine |
| 5 | `attack` — attaque (frelon asiatique, etc.) |

**Vecteur de features** — 17 dimensions par capteur (intérieur et extérieur) :

```
[0]    température (°C, z-scorée)
[1]    humidité (%, z-scorée)
[2]    fréquence dominante (Hz, z-scorée)
[3]    amplitude (z-scorée)
[4-16] MFCC c0…c12 (chacun z-scoré individuellement)
```

**Architecture** — apprentissage contrastif en deux phases :

- **Phase 1 — pré-entraînement non supervisé** : deux encodeurs MLP (intérieur et extérieur) entraînés par paires via perte InfoNCE. Aucun label requis.
- **Phase 2 — fine-tuning supervisé** : encodeurs gelés, tête de classification entraînée sur données labelisées (~30 événements × 10 paquets par classe).

**Scripts principaux** :

```bash
# Extraire les données d'entraînement depuis InfluxDB
python IA/collect_training_data.py --hive B001 --range 30d --out data/hive1.csv

# Pré-entraîner les encodeurs (non supervisé)
python IA/train_phase1.py

# Fine-tuner le classifieur (supervisé)
python IA/train_phase2.py
```

Une alerte `pre_swarming` est déclenchée lorsque P(pre_swarming) > 0,40 sur N = 3 relevés consécutifs (≈ 15 minutes à un intervalle de 5 minutes), pour éviter les faux positifs.

---

### 5. Serveur distant (`server/`)

Le serveur d'agrégation centralisé. Les Raspberry Pi lui poussent des lots de données scalaires ; l'application mobile Flutter l'interroge pour afficher les dashboards.

**Stack** : Flask 3, PostgreSQL (utilisateurs / API keys), InfluxDB 2, Gunicorn.

**Fonctionnalités** :

- Dashboard web affichant toutes les ruches de tous les Raspberry Pi connectés
- Gestion des utilisateurs (admin) : attribution du rôle viewer/admin, suppression
- Gestion des API keys (génération / révocation)
- **API REST mobile avec authentification JWT** (HS256, expiration 30 jours)
- Endpoint `/api/auth/verify` pour le pont JWT avec l'app Pi (voir [docs/AUTH.md](docs/AUTH.md))
- Réception des données poussées via `POST /api/push` (Bearer API key)

**Endpoints REST** :

| Méthode | Chemin | Auth | Description |
|---|---|---|---|
| `POST` | `/api/auth/login` | — | Connexion mobile → JWT |
| `POST` | `/api/auth/verify` | — | Vérifie identifiants et retourne JWT (pont Pi) |
| `POST` | `/api/auth/logout` | Bearer JWT | Déconnexion (côté client) |
| `GET` | `/api/beehives` | Bearer JWT / API key | Liste les ruches avec dernières valeurs |
| `GET` | `/api/beehives/<id>/data` | Bearer JWT / API key | Données graphe (`?range=24h`) |
| `POST` | `/api/push` | Bearer API key | Réception d'un lot depuis le Raspberry Pi |

**Variables d'environnement** (voir `server/.env.example`) :

| Variable | Description |
|---|---|
| `SECRET_KEY` | Clé secrète Flask (**différente** de celle de l'app web) |
| `JWT_SECRET_KEY` | Secret de signature JWT (partagé avec les instances app/ liées) |
| `DATABASE_URL` | Chaîne de connexion PostgreSQL |
| `INFLUXDB_URL` | URL InfluxDB |
| `INFLUXDB_TOKEN` | Token InfluxDB |
| `INFLUXDB_ORG` | Organisation InfluxDB |
| `INFLUXDB_BUCKET` | Bucket InfluxDB |

> `JWT_SECRET_KEY` est également la valeur à renseigner dans **app/ → Paramètres → Pont de compte distant** pour activer le bridge d'authentification.

---

### 6. Application mobile (`mobile/`)

Application Flutter cross-platform (Android + iOS) qui se connecte au serveur distant et affiche les données des ruches en temps réel.

**Stack** : Flutter, Dart, Riverpod (état), go_router (navigation), Dio (HTTP), fl_chart (graphes), flutter_secure_storage (stockage sécurisé), workmanager + flutter_local_notifications (alertes arrière-plan).

**Architecture** : providers Riverpod (`auth_provider`, `beehives_provider`) + services (`ApiService`, `StorageService`) + écrans + widgets.

**Fonctionnalités** :

- Connexion avec l'URL du serveur, nom d'utilisateur et mot de passe (JWT stocké de façon sécurisée)
- Dashboard listant toutes les ruches avec les 9 mesures scalaires
- Écran de détail d'une ruche : graphes température, humidité, fréquence sonore, amplitude et luminosité, plage de temps sélectionnable (1h/6h/24h/7d/30d)
- Alertes en arrière-plan toutes les 15 minutes via WorkManager
- Paramètres : seuils de notification, déconnexion

**Prérequis** : Android 8.0 (API 26) ou supérieur — iOS 12 ou supérieur (build depuis macOS + Xcode requis).

---

### 7. Simulateur (`tools/simulate.py`)

Outil de développement qui envoie de faux relevés réalistes (dérive sinusoïdale + bruit gaussien + MFCC simulés) via `POST /api/data`, en remplacement du matériel LoRa.

> La ruche doit déjà exister dans la base de données (crée-la via l'interface d'abord).

```bash
cd tools
pip install requests

# Mode live — un relevé toutes les 10 secondes, ruche B001
python simulate.py

# Plusieurs ruches
python simulate.py --ids B001 B002 B003

# Intervalle et URL personnalisés
python simulate.py --url http://raspberrypi.local:5000 --interval 5

# Mode burst — remplit 200 points historiques sur les 24 dernières heures
python simulate.py --burst 200
```

---

## Déploiement

### Application web — Raspberry Pi

**Prérequis** : Docker et Docker Compose.

```bash
cd app
cp .env.example .env
nano .env   # au minimum : SECRET_KEY et INFLUXDB_TOKEN

docker compose up -d --build
```

Ouvre `http://<ip-du-raspberry-pi>:5000`. Le premier compte inscrit reçoit automatiquement le rôle **admin**.

Démarre le récepteur LoRa (hors Docker, directement sur le Pi) :

```bash
cd lora
pip install -r requirements.txt
API_ENABLE=1 BEETTER_API_URL=http://localhost:5000 python3 receiver.py
```

**Lancer le récepteur en tant que service systemd** :

```ini
# /etc/systemd/system/beetter-lora.service
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

### Mises à jour (`deploy.sh`)

`deploy.sh`, à la racine du repo, automatise les déploiements suivants : `git pull`, rebuild Docker, redémarrage du service LoRa.

```bash
./deploy.sh                        # branche main, rebuild complet
./deploy.sh --no-build             # redémarre sans rebuild (templates/CSS/JS)
./deploy.sh --branch feature/audio
./deploy.sh --force                # écrase les modifications locales sans confirmation
```

Si des modifications locales non commitées sont détectées sur le Pi, le script les affiche et **demande confirmation** avant de les écraser (`git reset --hard` sur les fichiers suivis uniquement). `--force` saute la question.

### Mode kiosk au démarrage (optionnel)

Pour que le Pi ouvre automatiquement le dashboard en plein écran.

```bash
# ~/beetter-stack/start-dashboard.sh
#!/bin/bash
until curl -s -o /dev/null http://localhost:5000; do sleep 1; done
firefox --kiosk http://localhost:5000
```

```ini
# ~/.config/autostart/beetter-dashboard.desktop
[Desktop Entry]
Type=Application
Name=Beetter Dashboard
Exec=/home/<user>/beetter-stack/start-dashboard.sh
X-GNOME-Autostart-enabled=true
```

Activer l'autologin : `sudo raspi-config` → System Options → Boot / Auto Login → Desktop Autologin.

| Action | Raccourci |
|---|---|
| Fermer Firefox | `Alt+F4` |
| Basculer sur un terminal | `Ctrl+Alt+F2` |
| Revenir à l'interface graphique | `Ctrl+Alt+F1` |

---

### Serveur distant

```bash
cd server
cp .env.example .env
nano .env   # SECRET_KEY différent de l'app web

docker compose up -d --build
```

Ouvre `http://<ip-du-serveur>:5001`. Génère une API key : **Dashboard → API Keys → New key**.

### Connexion de l'application web au serveur distant

**Push de données** :

1. Ouvre `http://<ip-du-raspberry-pi>:5000` et connecte-toi.
2. Va dans **Paramètres → Ajouter un serveur**.
3. Renseigne l'URL du serveur, l'API key et l'intervalle de push (en minutes).
4. Sauvegarde. Le badge de statut passe au **vert** au premier push réussi.

> **Comportement hors ligne** : si le serveur est inaccessible, les données sont conservées et incluses dans le prochain push réussi. Le curseur `last_push_at` n'est avancé que sur les push réussis.

**Pont de compte (optionnel — pour l'authentification centralisée)** :

1. Va dans **Paramètres → Pont de compte distant** (admin seulement).
2. Renseigne l'URL du serveur et le `JWT_SECRET_KEY` configuré côté serveur.
3. Sauvegarde. Les utilisateurs peuvent désormais se connecter avec leurs identifiants du serveur central, même sans internet (le compte est mis en cache localement).

Voir [docs/AUTH.md](docs/AUTH.md) pour le détail du fonctionnement.

---

### Application mobile Flutter

**Prérequis** : [Flutter SDK](https://docs.flutter.dev/get-started/install) installé.

#### Build Android

```bash
cd mobile
flutter pub get
flutter build apk --release
```

```bash
# Installation directe
adb install build/app/outputs/flutter-apk/app-release.apk
```

#### Build iOS (macOS + Xcode requis)

```bash
cd mobile
flutter pub get
flutter build ios --release
```

#### Premier lancement

1. Entre l'URL du serveur distant — ex. `http://192.168.1.10:5001`.
2. Entre le nom d'utilisateur et le mot de passe.
3. Appuie sur **Se connecter**.

---

## Développement — démarrage rapide (sans Docker)

```bash
# Application web (Raspberry Pi)
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
cp .env.example .env             # édite SECRET_KEY, JWT_SECRET_KEY, DATABASE_URL / INFLUXDB_*
flask run --debug --port 5001

# Application mobile
cd mobile
flutter pub get
flutter run                      # sur appareil connecté ou émulateur
```

---

## Structure du projet

```
beetter/
├── deploy.sh               # Script de déploiement Pi (git pull + docker compose + systemd)
├── app/                    # Application web Raspberry Pi (Flask :5000)
│   ├── blueprints/
│   │   ├── api/            # POST /api/data — ingest JSON + données de graphe
│   │   ├── auth/           # Connexion / inscription / déconnexion
│   │   ├── account/        # Gestion du compte et préférences utilisateur
│   │   ├── admin/          # Administration des utilisateurs
│   │   ├── beehives/       # CRUD des ruches
│   │   ├── dashboard/      # Page d'accueil
│   │   ├── alerts/         # Journal des alertes
│   │   ├── settings/       # Configuration des serveurs distants
│   │   ├── calendar/       # Calendrier des événements de ruche
│   │   ├── export/         # Export CSV
│   │   ├── setup/          # Assistant de première configuration
│   │   └── utils/          # Helpers : InfluxDB (35 mesures), seuils, statut, push
│   ├── templates/
│   ├── static/
│   ├── models.py           # User, Beehive, Alert, RemoteServerConfig,
│   │                       #   HiveEvent, UserHiveIndicator, UserPreferences
│   ├── scheduler.py        # Job APScheduler de push
│   ├── migrate.py          # Création / mise à jour des tables
│   ├── compose.yml
│   └── Dockerfile
├── server/                 # Serveur d'agrégation distant (Flask :5001)
│   ├── blueprints/
│   │   ├── api/           # API REST mobile & push (JWT, /auth/verify)
│   │   ├── auth/
│   │   ├── dashboard/     # Dashboard + gestion utilisateurs (admin)
│   │   └── utils/         # Helpers InfluxDB
│   ├── models.py          # User, ApiKey
│   ├── compose.yml
│   └── Dockerfile
├── mobile/                # Application mobile Flutter (Android + iOS)
│   ├── lib/
│   │   ├── main.dart      # Entrée, WorkManager init, GoRouter
│   │   ├── theme/         # Design system (amber, dark sidebar)
│   │   ├── models/        # Beehive, SensorValue, ChartSeries
│   │   ├── services/      # ApiService (Dio + JWT), StorageService
│   │   ├── providers/     # auth_provider, beehives_provider (Riverpod)
│   │   ├── screens/       # login, dashboard, hive_detail, settings
│   │   └── widgets/       # BeehiveCard, LineChartCard, StatusDot
│   ├── android/           # Config Android (minSdk 26, fr.esiee.beetter)
│   ├── ios/               # Config iOS (bundle fr.esiee.beetter)
│   └── pubspec.yaml
├── Firmware/              # Firmware ESP32 (Arduino / C++)
│   ├── src/
│   │   ├── BeetterConfig.h               # Configuration (fréquence, SF, ID ruche…)
│   │   └── libraries/
│   │       ├── BeetterMic/               # Acquisition micro + MFCC 13 coefficients
│   │       ├── BeetterSHT40/             # Capteur température / humidité SHT40
│   │       ├── BeetterPhoto/             # Photorésistance (ADC)
│   │       ├── BeetterLoRa/              # Sérialisation et émission blocs ENV / AUD
│   │       ├── BeetterRTC/               # Horodatage RTC
│   │       └── BeetterSD/                # Journalisation SD (optionnel)
│   └── README.md
├── IA/                     # Pipeline ML (classification d'état de ruche)
│   ├── beehive/
│   │   ├── data.py         # Décodage des trames + feature engineering (17-d par capteur)
│   │   ├── model.py        # Encodeurs MLP + tête de classification (6 classes)
│   │   └── config.py       # Hyperparamètres (batch, lr, température InfoNCE…)
│   ├── collect_training_data.py  # Export CSV depuis InfluxDB
│   ├── train_phase1.py     # Pré-entraînement contrastif non supervisé
│   └── train_phase2.py     # Fine-tuning supervisé
├── lora/                   # Récepteur LoRa (Grove RFM95 868 MHz via UART/RadioHead)
│   ├── receiver.py         # Décode blocs ENV / AUD, envoie à l'API Flask
│   ├── grove_lora.py       # Pilote série du module Grove LoRa
│   └── requirements.txt
├── docs/                  # Documentation technique
│   ├── AUTH.md            # Architecture d'authentification centralisée (JWT)
│   └── API.md             # Référence complète de l'API REST
└── tools/
    └── simulate.py         # Simulateur de données capteurs (avec MFCC simulés)
```