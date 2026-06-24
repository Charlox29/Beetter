# Documentation technique — Serveur distant (`server/`)

Flask 3, Python 3.11, port **5001**. Hébergé sur un serveur cloud ou dédié ; agrège les données de plusieurs Raspberry Pi et sert l'API mobile.

---

## Architecture

```
Raspberry Pi(s)
   app/:5000
       │  POST /api/push (Bearer API key)
       ▼
  ┌───────────────────────────────┐
  │  server/:5001                 │
  │                               │
  │  api/         ← push ingest   │──▶ InfluxDB :8087
  │  api/         ← mobile REST   │◀── InfluxDB :8087
  │  auth/        ← web + JWT     │
  │  dashboard/   ← admin panel   │──▶ PostgreSQL
  └───────────────────────────────┘
           ▲
           │  GET /api/beehives
           │  Bearer JWT
       mobile/ app
```

---

## Initialisation (`server/__init__.py`)

**Extensions Flask** :

| Extension | Objet | Rôle |
|---|---|---|
| Flask-SQLAlchemy | `db` | ORM PostgreSQL |
| Flask-Bcrypt | `bcrypt` | Hachage des mots de passe |
| Flask-WTF CSRFProtect | `csrf` | Protection CSRF (exclut `/api/*`) |
| Flask-Login | `login_manager` | Sessions web (cookie) |

**Config JWT** :

```python
app.config['JWT_SECRET_KEY'] = os.environ.get('JWT_SECRET_KEY', app.config['SECRET_KEY'])
```

**Blueprints** :

| Blueprint | Préfixe | Rôle |
|---|---|---|
| `auth_bp` | `/auth` | Connexion / inscription / déconnexion (web) |
| `dashboard_bp` | `/dashboard` | Dashboard + gestion API keys + gestion utilisateurs |
| `api_bp` | `/api` | API REST (mobile + push Pi) |

---

## Modèles de données (`server/models.py`)

### `User`

```python
id            : Integer (PK)
username      : String(64), unique
email         : String(120), unique
password_hash : String(256)
role          : String(16)  # 'viewer' | 'admin'
created_at    : DateTime (UTC)

@property is_admin → role == 'admin'
```

### `ApiKey`

Clé d'authentification pour les Raspberry Pi (`POST /api/push`).

```python
id           : Integer (PK)
name         : String(64)
key          : String(64), unique   # hex aléatoire, affiché une seule fois
enabled      : Boolean  default True
last_used_at : DateTime (UTC)
created_at   : DateTime (UTC)
user_id      : FK → User.id
```

### `UserSession` *(legacy)*

Table conservée pour une éventuelle liste de révocation future. Les tokens mobiles sont des JWT stateless et ne sont plus stockés ici.

```python
id         : Integer (PK)
token      : String(64), unique
user_id    : FK → User.id
created_at : DateTime
expires_at : DateTime   # created_at + 30 jours
@property is_valid → datetime.now(utc) < expires_at
```

---

## API REST (`server/blueprints/api/routes.py`)

### Authentification unifiée

```python
def _authenticate():
    token = request.headers.get('Authorization', '').removeprefix('Bearer ')
    # 1. Essaie de décoder comme JWT HS256
    payload = _decode_jwt(token)
    if payload:
        return payload   # {'sub': username, 'role': role}
    # 2. Cherche comme ApiKey
    key = ApiKey.query.filter_by(key=token, enabled=True).first()
    if key:
        key.last_used_at = datetime.now(utc)
        db.session.commit()
        return key
    return None
```

### `POST /api/auth/login`

Connexion mobile. Retourne un JWT HS256.

**Requête** :
```json
{ "username": "alice", "password": "secret" }
```

**Réponse 200** :
```json
{
  "token": "<JWT>",
  "username": "alice",
  "role": "viewer",
  "expires_at": "2026-07-22T12:00:00+00:00"
}
```

**Réponse 401** : `{"error": "Invalid credentials"}`

### `POST /api/auth/verify`

Vérification + JWT pour le pont d'authentification de l'app Pi. Comportement identique à `/api/auth/login` mais sémantiquement différent (appelé machine-à-machine).

**Requête** :
```json
{ "username": "alice", "password": "secret" }
```

**Réponse 200** :
```json
{
  "valid": true,
  "token": "<JWT>",
  "username": "alice",
  "role": "viewer",
  "expires_at": "2026-07-22T12:00:00+00:00"
}
```

**Réponse 401** : `{"valid": false}`

### `POST /api/auth/logout`

Logout stateless (supprime le token côté client). Retourne `200 {"status": "ok"}`.

**Auth** : `Authorization: Bearer <JWT>`

### `POST /api/push`

Réception d'un lot de données depuis un Raspberry Pi.

**Auth** : `Authorization: Bearer <api_key>`

**Requête** :
```json
{
  "source": "beetter",
  "pushed_at": "2026-05-19T14:35:00Z",
  "beehives": [
    {
      "id": "B001",
      "name": "Ruche du verger",
      "location": "12 rue des Tilleuls, Noisy-le-Grand",
      "data": [
        {
          "timestamp": "2026-05-19T14:30:00Z",
          "temperature_int": 34.7,
          "humidity_int": 62.1
        }
      ]
    }
  ]
}
```

Chaque entrée `data` peut contenir un sous-ensemble des 9 mesures scalaires. Les points sont écrits dans InfluxDB avec les tags `beehive_id` et `source`.

**Réponse 200** :
```json
{ "status": "ok", "points_written": 42 }
```

### `GET /api/beehives`

**Auth** : `Authorization: Bearer <JWT>` ou `Bearer <api_key>`

**Réponse 200** :
```json
{
  "beehives": [
    {
      "id": "B001",
      "name": "Ruche du verger",
      "source": "beetter-pi-01",
      "latest": {
        "temperature_int": {"value": 34.7, "time": "2026-05-19T14:30:00Z"},
        "humidity_int":    {"value": 62.1, "time": "2026-05-19T14:30:00Z"},
        "temperature_ext": {"value": 18.3, "time": "2026-05-19T14:30:00Z"},
        "humidity_ext":    {"value": 55.0, "time": "2026-05-19T14:30:00Z"},
        "sound_freq_int":  {"value": 245.0,"time": "2026-05-19T14:30:00Z"},
        "sound_amp_int":   {"value": 0.0042,"time": "2026-05-19T14:30:00Z"},
        "sound_freq_ext":  {"value": 120.0,"time": "2026-05-19T14:30:00Z"},
        "sound_amp_ext":   {"value": 0.0011,"time": "2026-05-19T14:30:00Z"},
        "light_ext":       {"value": 7.6,  "time": "2026-05-19T14:30:00Z"}
      }
    }
  ]
}
```

### `GET /api/beehives/<id>/data`

**Auth** : `Authorization: Bearer <JWT>` ou `Bearer <api_key>`

**Paramètre** : `?range=24h` (`1h`, `6h`, `24h`, `7d`, `30d`)

**Réponse 200** :
```json
{
  "id": "B001",
  "range": "24h",
  "series": {
    "temperature_int": {
      "labels": ["2026-05-18T14:30:00Z", "..."],
      "data":   [34.7, 34.8, "..."]
    }
  }
}
```

---

## Dashboard admin (`server/blueprints/dashboard/routes.py`)

| Route | Auth | Description |
|---|---|---|
| `GET /` | login_required | Dashboard principal : ruches + API keys de l'utilisateur |
| `POST /api-keys/new` | login_required | Génère une nouvelle ApiKey (clé affichée une seule fois) |
| `POST /api-keys/<id>/delete` | login_required | Supprime une API key |
| `GET /beehives/<id>/chart-data` | login_required | Données AJAX pour les graphes Chart.js |
| `GET /users` | admin seulement | Liste tous les utilisateurs |
| `POST /users/<id>/toggle-role` | admin seulement | Bascule viewer ↔ admin |
| `POST /users/<id>/delete` | admin seulement | Supprime un utilisateur (protégé : ne peut pas se supprimer soi-même) |

---

## JWT — détails d'implémentation

```python
import jwt  # PyJWT 2.8.0

def _issue_jwt(user):
    now = datetime.now(timezone.utc)
    payload = {
        'sub':  user.username,
        'role': user.role,
        'iat':  now,
        'exp':  now + timedelta(days=30),
    }
    return jwt.encode(payload, current_app.config['JWT_SECRET_KEY'], algorithm='HS256')

def _decode_jwt(token):
    try:
        return jwt.decode(token, current_app.config['JWT_SECRET_KEY'], algorithms=['HS256'])
    except jwt.PyJWTError:
        return None
```

Les tokens **expirent après 30 jours**. Il n'y a pas de refresh token — l'utilisateur doit se reconnecter. Pour révoquer tous les tokens (compromission), changer `JWT_SECRET_KEY` suffit.

---

## InfluxDB (`server/blueprints/utils/influxdb.py`)

**Organisation** : `beetter_srv`  
**Bucket** : `sensors`  
**Port externe** : `8087` (mappé vers le port interne `8086` du conteneur)

```python
write_push_data(beehive_id, data_points, source)
    # Écrit une liste de dicts {timestamp, measurement: value}
    # Tags : beehive_id, source

query_chart_data(beehive_id, range)
    # Agrège et retourne les 9 mesures scalaires

list_beehives()
    # Retourne {beehive_id: {measurement: {value, time}}}
    # utilisé par GET /api/beehives
```

---

## Variables d'environnement (`server/.env.example`)

| Variable | Description |
|---|---|
| `SECRET_KEY` | Clé secrète Flask (**différente** de celle de l'app Pi) |
| `JWT_SECRET_KEY` | Secret de signature JWT (partagé avec les app/ Pi liées) |
| `DATABASE_URL` | `postgresql://beetter_srv:beetter_srv@db:5432/beetter_srv` |
| `INFLUXDB_URL` | `http://influxdb:8086` (interne Docker) |
| `INFLUXDB_TOKEN` | Token InfluxDB |
| `INFLUXDB_ORG` | `beetter_srv` |
| `INFLUXDB_BUCKET` | `sensors` |

---

## Docker (`server/compose.yml`)

```
Service   Port ext.   Port int.   Image                   Volumes
────────────────────────────────────────────────────────────────────
server    5001        5001        python:3.11-slim         (code)
          gunicorn 2 workers / timeout 120s
db        —           5432        postgres:16-alpine       server_postgres_data
          POSTGRES_USER=beetter_srv  DB=beetter_srv
influxdb  8087        8086        influxdb:2.7-alpine      server_influxdb_data
          org=beetter_srv / init_bucket=sensors
```

**Pourquoi 2 workers ?** Le serveur reçoit moins de requêtes que l'app Pi (pas de réception LoRa permanente) et nécessite un timeout plus long (120s) pour les gros push de données.
