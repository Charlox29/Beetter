# Documentation technique — Application web Pi (`app/`)

Flask 3, Python 3.11, port **5000**. Tourne sur le Raspberry Pi ; c'est le cœur opérationnel de Beetter.

---

## Architecture

```
              POST /api/data
lora/receiver.py ──────────────────────────────────────────────────────▶ api/
tools/simulate.py ─────────────────────────────────────────────────────▶ api/
                                                                          │
                                                            write()       ▼
                                                        InfluxDB (local)
                                                              │
                                          query()            ▼
                    browser ◀── dashboard/ ◀─────────── InfluxDB
                                    │
                                    │ PostgreSQL (ruches, alertes, utilisateurs)
                                    ▼
                    APScheduler (toutes les minutes)
                         │
                         ├─ push_to_remote() ──POST /api/push──▶ server/:5001
                         ├─ check_no_data()
                         └─ generate_daily_summaries()
```

---

## Initialisation (`app/__init__.py`)

**Extensions Flask** :

| Extension | Objet | Rôle |
|---|---|---|
| Flask-SQLAlchemy | `db` | ORM PostgreSQL |
| Flask-Bcrypt | `bcrypt` | Hachage des mots de passe |
| Flask-WTF CSRFProtect | `csrf` | Protection CSRF (exclut `/api/*`) |
| Flask-Login | `login_manager` | Sessions web (cookie) |
| APScheduler | `scheduler` | Jobs périodiques |

**Blueprints enregistrés** :

| Blueprint | Préfixe | Fichier |
|---|---|---|
| `auth_bp` | `/auth` | `blueprints/auth/routes.py` |
| `dashboard_bp` | `/dashboard` | `blueprints/dashboard/routes.py` |
| `beehives_bp` | `/beehives` | `blueprints/beehives/routes.py` |
| `settings_bp` | `/settings` | `blueprints/settings/routes.py` |
| `account_bp` | `/account` | `blueprints/account/routes.py` |
| `api_bp` | `/api` | `blueprints/api/routes.py` |
| `alerts_bp` | `/alerts` | `blueprints/alerts/routes.py` |
| `export_bp` | `/export` | `blueprints/export/routes.py` |
| `admin_bp` | `/admin` | `blueprints/admin/routes.py` |
| `calendar_bp` | `/calendar` | `blueprints/calendar/routes.py` |
| `setup_bp` | `/setup` | `blueprints/setup/routes.py` |

---

## Modèles de données (`app/models.py`)

### `User`

```python
id            : Integer (PK)
username      : String(16), unique, not null
email         : String(120), unique, not null
password_hash : String(256)
role          : String(16)   # 'viewer' | 'editor' | 'admin'
created_at    : DateTime (UTC)
```

Le premier compte inscrit reçoit automatiquement le rôle `admin`.

### `Beehive`

```python
id                       : String(4) (PK)   # ASCII ex. "B001"
name                     : String(64), not null
street                   : String(128)
city                     : String(64)
postal_code              : String(16)
latitude                 : Float
longitude                : Float
device_eui               : String(16)        # identifiant LoRa matériel
lora_frequency           : Float    default 868.0
spreading_factor         : Integer  default 7
bandwidth                : Integer  default 125
enabled                  : Boolean  default True
status                   : Enum('calm','foraging','ventilating','stressed',
                                 'agitated','virgin_queen','critical',
                                 'swarming','queenless','predator',
                                 'silent','no_data')
no_data_threshold_minutes: Integer  default 10
created_at               : DateTime (UTC)
user_id                  : FK → User.id
```

### `Alert`

```python
id         : Integer (PK)
hive_id    : FK → Beehive.id
old_status : String
new_status : String
source     : String   # 'manual' | 'threshold'
note       : Text
created_at : DateTime (UTC)
# M2M avec User via table intermédiaire user_alert_reads
```

### `RemoteServerConfig`

```python
id               : Integer (PK)
name             : String(64)
url              : String(256)     # URL base du server/
api_key          : String(128)     # Bearer API key pour /api/push
push_interval    : Integer  default 10   # minutes entre pushes
enabled          : Boolean  default True
last_push_at     : DateTime (UTC)  # curseur — avancé UNIQUEMENT sur succès
last_push_status : String   # 'success'|'error'|'offline'|'timeout'
last_push_message: String(500)
created_at       : DateTime (UTC)
user_id          : FK → User.id
```

### `HiveEvent`

```python
id         : Integer (PK)
title      : String(128)
event_type : String(32)   # 'inspection'|'treatment'|'harvest'|'other'
start_date : DateTime
end_date   : DateTime
all_day    : Boolean
notes      : Text
hive_id    : FK → Beehive.id
created_by : FK → User.id
```

### `UserHiveIndicator`

Indicateurs affichés par ruche et par utilisateur sur le dashboard.

```python
user_id    : FK → User.id  (PK composite)
hive_id    : FK → Beehive.id  (PK composite)
indicators : String   # CSV ex. "temperature_int,humidity_int"
```

### `UserPreferences`

```python
user_id        : FK → User.id (unique)
language       : String  # 'en' | 'fr'
time_format    : String  # '24h' | '12h'
week_start     : String  # 'monday' | 'sunday'
temp_unit      : String  # 'C' | 'F'
email_alerts   : Boolean
alert_temperature : Boolean
alert_humidity    : Boolean
alert_sound       : Boolean
alert_offline     : Boolean
high_contrast  : Boolean
large_text     : Boolean
reduce_motion  : Boolean
updated_at     : DateTime
```

### `SystemConfig`

Table clé-valeur pour les paramètres d'instance.

```python
key   : String(64)  (PK)
value : String(255)
```

**Clés utilisées** :

| Clé | Valeur par défaut | Description |
|---|---|---|
| `summary_hour` | `1` | Heure UTC pour la génération des résumés quotidiens |
| `linked_server_url` | _(vide)_ | URL du server/ lié pour le pont JWT |
| `linked_server_jwt_secret` | _(vide)_ | Secret JWT partagé avec le server/ lié |

### `DailySummary`

```python
id             : Integer (PK)
hive_id        : FK → Beehive.id
date           : Date (unique par ruche)
avg_temp_int   : Float
avg_temp_ext   : Float
avg_hum_int    : Float
avg_freq_int   : Float
avg_amp_int    : Float
avg_light      : Float
alert_count    : Integer
status_at_end  : String
data_points    : Integer
generated_at   : DateTime
```

---

## Pipeline de données

### Ingest (`POST /api/data`)

```
1. Vérifie que beehive_id existe en PostgreSQL
2. Écrit les valeurs présentes dans InfluxDB :
   - 9 mesures scalaires (une measurement par grandeur)
   - 0 à 26 MFCC si mfcc_int/mfcc_ext sont des listes de 13 floats
3. Vérifie les seuils :
   - Récupère les N derniers points bruts par query_recent_points()
   - CONSECUTIVE_CRIT = 3 points hors zone ok → status 'critical'
   - CONSECUTIVE_WARN = 5 points hors zone ok → status 'agitated'
   - CONSECUTIVE_OK   = 3 points dans zone ok → retour auto au calme
   - Crée une Alert (source='threshold') à chaque changement de statut
4. Répond 201 {"status": "ok"}
```

### Seuils (`app/blueprints/utils/thresholds.py`)

| Mesure | Zone OK | Zone Warning |
|---|---|---|
| `temperature_int` | 32 – 38 °C | 28 – 42 °C |
| `temperature_ext` | 5 – 35 °C | 0 – 40 °C |
| `humidity_int` | 50 – 75 % | 40 – 85 % |
| `humidity_ext` | 30 – 90 % | 20 – 95 % |
| `sound_freq_int` | 180 – 320 Hz | 150 – 400 Hz |
| `light_ext` | 0 – 5 | 0 – 10 |

### Système de statuts (`app/blueprints/utils/status.py`)

```
Famille 'calm'     : calm, foraging, ventilating      → badge vert
Famille 'agitated' : stressed, agitated, virgin_queen → badge orange
Famille 'critical' : critical, swarming, queenless, predator → badge rouge
Aucune famille     : silent, no_data                  → badge gris
```

---

## Mesures InfluxDB (`app/blueprints/utils/influxdb.py`)

**Schéma par point** : `measurement=<nom>`, tag `beehive_id`, field `value`.

**35 mesures totales** (`MEASUREMENTS`) :

| # | Mesure | Dashboard | Push serveur |
|---|---|---|---|
| 1–9 | `temperature_int` … `light_ext` | oui (`CHART_MEASUREMENTS`) | oui |
| 10–22 | `mfcc_int_0` … `mfcc_int_12` | non | non |
| 23–35 | `mfcc_ext_0` … `mfcc_ext_12` | non | non |

**Fenêtres d'agrégation pour les graphes** :

| Plage | Agrégation |
|---|---|
| 1h | 1 min |
| 6h | 2 min |
| 24h | 15 min |
| 7d | 2 h |
| 30d | 12 h |

**Fonctions principales** :

```python
write_sensor_data(beehive_id, temp_int, hum_int, temp_ext, hum_ext,
                  sound_freq_int, sound_amp_int, sound_freq_ext, sound_amp_ext,
                  light_ext, mfcc_int, mfcc_ext, timestamp)

query_chart_data(beehive_id, range)        # → {measurement: {labels, data}}
query_latest_values(beehive_id)            # → {measurement: {value, time}}
query_recent_points(beehive_id, sensor_key, n=5)  # → List[float]
query_export_data(beehive_id, measurements, start_str, stop_str)
query_recent_data(beehive_id, since)       # → [{timestamp, measurement: value}]
```

---

## Jobs planifiés (`app/scheduler.py`)

Tous les jobs tournent via **APScheduler** (BackgroundScheduler, timezone UTC).

### `_check_and_push()` — toutes les 1 minute

Pour chaque `RemoteServerConfig` activée :
1. Calcule si `now - last_push_at >= push_interval`
2. Si oui, appelle `push_to_remote(config_id)`
3. `push_to_remote()` récupère tous les points depuis `last_push_at` et les POSTs sur `{url}/api/push`
4. **Curseur `last_push_at` : avancé uniquement si HTTP 200.** Sur `ConnectionError` ou `Timeout`, le statut passe à `offline`/`timeout` mais les données sont conservées pour le prochain push.

### `_check_no_data()` — toutes les 1 minute

Pour chaque ruche activée : si la dernière mesure InfluxDB date de plus de `no_data_threshold_minutes` minutes → status = `no_data`, alerte `source='threshold'`.

### `_generate_daily_summaries()` — quotidien à `summary_hour` UTC

Calcule moyennes (temp_int/ext, hum_int, freq_int, amp_int, light) et nombre d'alertes pour hier, et stocke un `DailySummary` par ruche.

---

## Authentification (`app/blueprints/auth/routes.py`)

### Flux de connexion

```
1. Utilisateur soumet username + password
2. Recherche locale (User.query.filter_by(username=...))
3a. Trouvé → vérifie password_hash (bcrypt) → Flask-Login
3b. Non trouvé ET linked_server_url configuré → _try_remote_login()
      POST {linked_server_url}/api/auth/verify
      Si valid=True :
        - Vérifie signature JWT avec linked_server_jwt_secret (local, offline-safe)
        - Crée/met à jour User local (username, role, password haché)
        - Flask-Login
      Si timeout/erreur réseau → affiche "Invalid credentials"
```

Les comptes créés via `/register` sont **locaux** et ne font jamais l'objet d'une vérification distante.

---

## Variables d'environnement (`app/.env.example`)

| Variable | Défaut | Description |
|---|---|---|
| `SECRET_KEY` | — | Clé secrète Flask (chaîne aléatoire longue) |
| `DATABASE_URL` | `postgresql://beetter:beetter@db:5432/beetter` | Connexion PostgreSQL |
| `INFLUXDB_URL` | `http://influxdb:8086` | URL InfluxDB (interne Docker) |
| `INFLUXDB_TOKEN` | — | Token d'authentification InfluxDB |
| `INFLUXDB_ORG` | `beetter` | Organisation InfluxDB |
| `INFLUXDB_BUCKET` | `sensors` | Bucket des mesures capteurs |
| `INFLUXDB_PREDICTIONS_BUCKET` | `predictions` | Bucket des prédictions ML (création manuelle) |

---

## Docker (`app/compose.yml`)

```
Services      Port        Image                     Volumes
─────────────────────────────────────────────────────────────────
app           5000        python:3.11-slim          (code monté)
              gunicorn 4 workers / gevent / timeout 30s
db            (interne)   postgres:16-alpine        postgres_data
influxdb      8086        influxdb:2.7-alpine       influxdb_data
              DOCKER_INFLUXDB_INIT_BUCKET=sensors
              org=beetter / username=admin
```

**Commandes Gunicorn** : `gunicorn --bind 0.0.0.0:5000 --workers 4 --worker-class gevent --timeout 30 "app:create_app()"`
