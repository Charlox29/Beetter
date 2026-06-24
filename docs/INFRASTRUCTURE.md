# Infrastructure — Schémas et données techniques

---

## Vue d'ensemble — Topologie réseau

```
                    ┌──────────────────────────────────────────────┐
                    │  TERRAIN (Ruche)                             │
                    │                                              │
                    │  [ESP32 Intérieur]   [ESP32 Extérieur]       │
                    │  SHT40 + Micro        SHT40 + Micro          │
                    │  MFCC (13 coef.)      MFCC + Photorésistance  │
                    └───────────────┬──────────────────────────────┘
                                    │  LoRa 868 MHz
                                    │  Trames binaires (ENV + AUD)
                    ┌───────────────▼──────────────────────────────┐
                    │  RÉSEAU LOCAL (Apiculteur)                   │
                    │                                              │
                    │  ┌────────────────────────────────────────┐  │
                    │  │  Raspberry Pi 4                        │  │
                    │  │                                        │  │
                    │  │  lora/receiver.py ◄── Grove LoRa 868   │  │
                    │  │       │                                │  │
                    │  │       │ POST /api/data                 │  │
                    │  │       ▼                                │  │
                    │  │  app/ Flask :5000                      │  │
                    │  │  ├── PostgreSQL :5432 (ruches,users)   │  │
                    │  │  └── InfluxDB :8086 (mesures)          │  │
                    │  │       │                                │  │
                    │  │       │ POST /api/push (Bearer API key) │  │
                    │  └───────┼────────────────────────────────┘  │
                    └──────────┼───────────────────────────────────┘
                               │  Internet (HTTPS recommandé)
                    ┌──────────▼───────────────────────────────────┐
                    │  CLOUD / SERVEUR DÉDIÉ                       │
                    │                                              │
                    │  server/ Flask :5001                         │
                    │  ├── PostgreSQL :5432 (users, api_keys)      │
                    │  └── InfluxDB :8087 (mesures agrégées)       │
                    │                                              │
                    └──────────┬───────────────────────────────────┘
                               │  HTTPS REST (Bearer JWT)
                    ┌──────────▼───────────────────────────────────┐
                    │  MOBILE (Android / iOS)                      │
                    │                                              │
                    │  mobile/ Flutter                             │
                    │  ├── JWT stocké (Keystore / Keychain)        │
                    │  └── WorkManager alertes (15 min)            │
                    └──────────────────────────────────────────────┘
```

---

## Flux de données détaillé

### 1. Acquisition capteurs → InfluxDB local

```
ESP32 intérieur                    ESP32 extérieur
    │  Trame ENV (23 B, 0xE0)           │  Trame ENV (23 B, 0xE0)
    │  Trame AUD (73 B, 0xA0)           │  Trame AUD (73 B, 0xA0)
    └─────────────┬─────────────────────┘
                  │  LoRa 868 MHz (UART /dev/serial0)
                  ▼
        lora/receiver.py
         ├── Décode blocs ENV / AUD
         ├── Vérifie CRC-16/CCITT
         ├── Détecte pertes de trames (numéro de séquence)
         └── POST http://localhost:5000/api/data
                  │  JSON : {beehive_id, temperature_int, ..., mfcc_int[13], mfcc_ext[13]}
                  ▼
        app/ POST /api/data
         ├── Vérifie beehive_id (PostgreSQL)
         ├── write_sensor_data() → InfluxDB
         │    └── 9 scalaires + 26 MFCC = 35 points par relevé
         ├── query_recent_points() → vérifie seuils
         └── Crée Alert si changement de statut
```

### 2. Push vers le serveur distant

```
APScheduler (toutes les minutes)
    │
    ├── Pour chaque RemoteServerConfig activée :
    │     ├── Calcule if now - last_push_at >= push_interval (default 10 min)
    │     ├── query_recent_data(beehive_id, since=last_push_at)
    │     │    └── 9 scalaires seulement (pas de MFCC)
    │     ├── POST {server_url}/api/push
    │     │    Bearer: <api_key>
    │     │    Body: {source, pushed_at, beehives: [{id, data:[...]}]}
    │     │
    │     ├── Si HTTP 200 :
    │     │    last_push_at = now    ← UNIQUEMENT ici
    │     │    last_push_status = 'success'
    │     │
    │     └── Si ConnectionError / Timeout :
    │          last_push_status = 'offline' / 'timeout'
    │          last_push_at INCHANGÉ ← les données seront réessayées
    │
    └── check_no_data() : si dernier point > no_data_threshold_minutes → status='no_data'
```

### 3. Consultation mobile

```
mobile/ Flutter
    │
    ├── Démarrage : GoRouter vérifie authProvider
    │    └── token présent → DashboardScreen
    │         → GET {server_url}/api/beehives
    │              Authorization: Bearer <JWT>
    │              ← {beehives: [{id, latest: {temperature_int: {value,time}, ...}}]}
    │
    ├── Tap ruche → HiveDetailScreen
    │    → GET {server_url}/api/beehives/{id}/data?range=24h
    │         ← {series: {temperature_int: {labels, data}, ...}}
    │
    └── WorkManager (15 min, si réseau disponible)
         → GET /api/beehives
         → Comparer valeurs contre seuils (FlutterSecureStorage)
         → flutter_local_notifications si dépassement
```

---

## Schéma Docker — Raspberry Pi (`app/compose.yml`)

```
┌──────────────────────────────────────────────────┐
│  Docker réseau : beetter_network                 │
│                                                  │
│  ┌──────────────────┐    ┌──────────────────┐    │
│  │  app             │    │  db              │    │
│  │  python:3.11-slim│    │  postgres:16-alp │    │
│  │  :5000 (exposé)  │◄──▶│  :5432 (interne) │    │
│  │  gunicorn         │    │  vol:postgres_data│   │
│  │  4 workers/gevent│    └──────────────────┘    │
│  │  timeout 30s     │                            │
│  │                  │    ┌──────────────────┐    │
│  │                  │◄──▶│  influxdb        │    │
│  └──────────────────┘    │  influxdb:2.7-alp│    │
│                          │  :8086 (interne) │    │
│                          │  vol:influxdb_data│   │
│                          │  bucket: sensors  │   │
│                          └──────────────────┘    │
└──────────────────────────────────────────────────┘
         ▲ Port 5000 exposé sur l'hôte Pi
```

---

## Schéma Docker — Serveur distant (`server/compose.yml`)

```
┌──────────────────────────────────────────────────┐
│  Docker réseau : server_network                  │
│                                                  │
│  ┌──────────────────┐    ┌──────────────────┐    │
│  │  server          │    │  db              │    │
│  │  python:3.11-slim│    │  postgres:16-alp │    │
│  │  :5001 (exposé)  │◄──▶│  :5432 (interne) │    │
│  │  gunicorn         │    │  vol:srv_pg_data │    │
│  │  2 workers        │    │  user:beetter_srv│    │
│  │  timeout 120s    │    └──────────────────┘    │
│  │                  │                            │
│  │                  │    ┌──────────────────┐    │
│  │                  │◄──▶│  influxdb        │    │
│  └──────────────────┘    │  influxdb:2.7-alp│    │
│                          │  :8086 (interne) │    │
│                          │  :8087 (exposé)  │    │
│                          │  vol:srv_idb_data│    │
│                          │  org:beetter_srv  │   │
│                          └──────────────────┘    │
└──────────────────────────────────────────────────┘
  Ports exposés : 5001 (Flask), 8087 (InfluxDB optionnel)
```

---

## Schéma de la base PostgreSQL — App Pi

```
┌─────────────────┐      ┌─────────────────────────┐
│ User            │      │ Beehive                  │
│─────────────────│      │─────────────────────────│
│ id (PK)         │──┐   │ id String(4) (PK)        │
│ username        │  │   │ name                     │
│ email           │  │   │ street / city / postal   │
│ password_hash   │  │   │ latitude / longitude     │
│ role            │  │   │ device_eui               │
│ created_at      │  │   │ lora_frequency/SF/BW     │
└─────────────────┘  │   │ enabled                  │
                     │   │ status (enum 12 vals)    │
                     │   │ no_data_threshold_min    │
                     │   │ created_at               │
                     │   │ user_id (FK → User)      │
                     │   └────────────┬────────────┘
                     │                │
                     │   ┌────────────▼────────────┐
                     │   │ Alert                    │
                     │   │─────────────────────────│
                     │   │ id (PK)                  │
                     │   │ hive_id (FK → Beehive)   │
                     │   │ old_status / new_status  │
                     │   │ source (manual/threshold)│
                     │   │ note                     │
                     │   │ created_at               │
                     │   └─────────────────────────┘
                     │
                     │   ┌─────────────────────────┐
                     │   │ RemoteServerConfig       │
                     │   │─────────────────────────│
                     │   │ id (PK)                  │
                     │   │ name / url / api_key     │
                     │   │ push_interval            │
                     │   │ enabled                  │
                     │   │ last_push_at             │
                     │   │ last_push_status         │
                     │   │ last_push_message        │
                     │   │ user_id (FK → User)      │
                     │   └─────────────────────────┘
                     │
                     │   ┌─────────────────────────┐
                     │   │ HiveEvent                │
                     │   │─────────────────────────│
                     │   │ id (PK)                  │
                     │   │ title / event_type       │
                     │   │ start_date / end_date    │
                     │   │ all_day / notes          │
                     │   │ hive_id (FK → Beehive)   │
                     │   │ created_by (FK → User)   │
                     │   └─────────────────────────┘
                     │
                     └── UserHiveIndicator (PK: user_id + hive_id)
                         UserPreferences (FK → User, unique)
                         DailySummary (FK → Beehive, unique/date)

SystemConfig (key-value)
  summary_hour | linked_server_url | linked_server_jwt_secret
```

---

## Schéma de la base PostgreSQL — Serveur distant

```
┌─────────────────┐      ┌─────────────────────────┐
│ User            │      │ ApiKey                   │
│─────────────────│      │─────────────────────────│
│ id (PK)         │──┐   │ id (PK)                  │
│ username        │  │   │ name                     │
│ email           │  │   │ key  String(64)          │
│ password_hash   │  │   │ enabled                  │
│ role viewer/adm │  └──▶│ last_used_at             │
│ created_at      │      │ created_at               │
└─────────────────┘      │ user_id (FK → User)      │
                         └─────────────────────────┘

UserSession (legacy — non utilisé pour les JWT mobiles)
  id | token String(64) | user_id (FK) | expires_at
```

---

## Schéma InfluxDB

**App Pi** — org `beetter`, bucket `sensors` :

```
measurement        tag            field   exemple
──────────────────────────────────────────────────
temperature_int    beehive_id=B001  value  34.7
humidity_int       beehive_id=B001  value  62.1
temperature_ext    beehive_id=B001  value  18.3
humidity_ext       beehive_id=B001  value  55.0
sound_freq_int     beehive_id=B001  value  245.0
sound_amp_int      beehive_id=B001  value  0.0042
sound_freq_ext     beehive_id=B001  value  120.0
sound_amp_ext      beehive_id=B001  value  0.0011
light_ext          beehive_id=B001  value  7.6
mfcc_int_0         beehive_id=B001  value  1.20
mfcc_int_1 … 12   beehive_id=B001  value  ...
mfcc_ext_0 … 12   beehive_id=B001  value  ...
```

**Serveur distant** — org `beetter_srv`, bucket `sensors` :

```
measurement        tags                      field   exemple
──────────────────────────────────────────────────────────────
temperature_int    beehive_id=B001, source=pi-01  value  34.7
...                (9 scalaires uniquement, pas de MFCC)
```

---

## Ports — Référence complète

| Composant | Service | Port interne | Port exposé | Protocole |
|---|---|---|---|---|
| app/ Pi | Flask (Gunicorn) | 5000 | 5000 | HTTP |
| app/ Pi | PostgreSQL | 5432 | — | TCP interne |
| app/ Pi | InfluxDB | 8086 | — | HTTP interne |
| server/ | Flask (Gunicorn) | 5001 | 5001 | HTTP (→ HTTPS via proxy) |
| server/ | PostgreSQL | 5432 | — | TCP interne |
| server/ | InfluxDB | 8086 | 8087 | HTTP (optionnel) |
| lora/ | — | — | /dev/serial0 | UART série |

---

## Variables d'environnement — Référence

### App Pi (`app/.env`)

| Variable | Défaut Docker | Description |
|---|---|---|
| `SECRET_KEY` | *à définir* | Clé secrète Flask |
| `DATABASE_URL` | `postgresql://beetter:beetter@db:5432/beetter` | PostgreSQL |
| `INFLUXDB_URL` | `http://influxdb:8086` | InfluxDB (interne) |
| `INFLUXDB_TOKEN` | *à définir* | Token InfluxDB |
| `INFLUXDB_ORG` | `beetter` | Organisation InfluxDB |
| `INFLUXDB_BUCKET` | `sensors` | Bucket données capteurs |
| `INFLUXDB_PREDICTIONS_BUCKET` | `predictions` | Bucket ML (création manuelle) |

### Serveur distant (`server/.env`)

| Variable | Défaut Docker | Description |
|---|---|---|
| `SECRET_KEY` | *à définir* | Clé secrète Flask (**différente** de app/) |
| `JWT_SECRET_KEY` | *à définir* | Secret JWT (partagé avec les Pi liés) |
| `DATABASE_URL` | `postgresql://beetter_srv:beetter_srv@db:5432/beetter_srv` | PostgreSQL |
| `INFLUXDB_URL` | `http://influxdb:8086` | InfluxDB (interne) |
| `INFLUXDB_TOKEN` | *à définir* | Token InfluxDB |
| `INFLUXDB_ORG` | `beetter_srv` | Organisation InfluxDB |
| `INFLUXDB_BUCKET` | `sensors` | Bucket données |

### Récepteur LoRa (`lora/`)

| Variable | Défaut | Description |
|---|---|---|
| `API_ENABLE` | `0` | `1` pour activer l'envoi vers Flask |
| `BEETTER_API_URL` | `http://localhost:5000` | URL Flask |
| `BEETTER_API_TIMEOUT` | `5` | Timeout HTTP (secondes) |
| `USE_LORA_TIMESTAMP` | `0` | `1` = horodatage firmware, `0` = horodatage Pi |

---

## Authentification — Vue d'ensemble comparée

| Plateforme | Mécanisme | Stockage token | Durée |
|---|---|---|---|
| app/ Pi (web) | Flask-Login (cookie session) | Cookie HTTP (server-side) | Session navigateur |
| app/ Pi (API ingest) | Pas d'auth sur `/api/data` | — | — |
| app/ Pi → server/ (push) | Bearer API key | `RemoteServerConfig.api_key` (PostgreSQL) | Permanent (révocation manuelle) |
| server/ (web) | Flask-Login (cookie session) | Cookie HTTP (server-side) | Session navigateur |
| server/ → app/ (JWT bridge) | JWT HS256 | `SystemConfig` (PostgreSQL) | 30 jours |
| mobile/ → server/ | JWT HS256 | FlutterSecureStorage | 30 jours |

---

## Capacités de montée en charge

| Goulot | Configuration actuelle | Limite indicative |
|---|---|---|
| app/ Pi workers | 4 (gevent, timeout 30s) | ~50 req/s (Pi 4) |
| server/ workers | 2 (timeout 120s) | ~20 req/s concurrentes |
| InfluxDB écriture | 1 bucket, 1 Pi | Pas de limite pratique pour un Pi |
| InfluxDB lecture | Agrégation Flux | Fenêtres larges (7d/30d) peuvent être lentes sur gros volumes |
| PostgreSQL Pi | 1 instance locale | Centaines de ruches (non testé au-delà de 10) |
| Push intervalle | 10 min par défaut | Ajustable dans RemoteServerConfig |

---

## Composants non inclus dans ce déploiement

| Composant | Raison |
|---|---|
| Reverse proxy HTTPS | À configurer manuellement (Nginx, Caddy) |
| Bucket InfluxDB `predictions` | Création manuelle requise (ML) |
| Certificat TLS | Let's Encrypt / Certbot |
| Monitoring/alerting serveur | Prometheus, Grafana, etc. (hors scope) |
| Sauvegarde automatisée | cron + pg_dump à configurer (voir DEPLOYMENT.md) |
