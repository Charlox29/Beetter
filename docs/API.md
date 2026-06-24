# Référence API REST

Beetter expose deux API REST indépendantes : l'une sur l'**app Pi** (`app/`, port 5000) et l'autre sur le **serveur distant** (`server/`, port 5001).

---

## API app/ Pi (port 5000)

### Authentification

Les endpoints `/api/*` utilisent soit une **clé API** (header `Authorization: Bearer <api_key>`), soit la **session Flask-Login** (cookie de session du dashboard web).

---

### POST `/api/data` — Ingest d'un relevé

Envoie un relevé de capteurs à stocker dans InfluxDB. Utilisé par `lora/receiver.py` et `tools/simulate.py`.

**Pas d'authentification requise** sur ce endpoint (accès interne Raspberry Pi).

**Corps** :

```json
{
  "beehive_id": "B001",
  "temperature_int": 34.7,
  "humidity_int": 62.1,
  "temperature_ext": 18.3,
  "humidity_ext": 55.0,
  "sound_freq_int": 245.0,
  "sound_amp_int": 0.0042,
  "sound_freq_ext": 120.0,
  "sound_amp_ext": 0.0011,
  "light_ext": 7.6,
  "mfcc_int": [1.2, -0.5, 0.3, 0.7, -1.1, 0.4, 0.0, -0.2, 0.9, 0.1, -0.3, 0.6, -0.8],
  "mfcc_ext": [0.8, -1.1, 0.6, 0.2, -0.4, 0.5, 0.1, -0.7, 1.0, 0.3, -0.1, 0.4, -0.6],
  "timestamp": "2026-05-19T14:32:00Z"
}
```

Tous les champs capteurs sont **optionnels** sauf `beehive_id`. `timestamp` est optionnel (défaut : heure de réception). `mfcc_int` / `mfcc_ext` doivent contenir exactement 13 floats ; si absents ou de mauvaise longueur, les MFCC sont ignorés.

**Réponse** : `201 {"status": "ok"}`

Après écriture dans InfluxDB, la route vérifie les seuils et crée automatiquement une alerte si une valeur dépasse un seuil warning ou critique.

---

### GET `/api/beehives/<id>/chart` — Données de graphe (app Pi)

Retourne les séries temporelles d'une ruche pour les graphes du dashboard local.

**Paramètres** : `?range=24h` (valeurs acceptées : `1h`, `6h`, `24h`, `7d`, `30d`)

**Réponse** :

```json
{
  "labels": ["2026-05-19T12:00:00Z", "..."],
  "datasets": {
    "temperature_int": [34.7, 34.8, "..."],
    "humidity_int": [62.1, 62.0, "..."],
    "temperature_ext": [18.3, "..."],
    "humidity_ext": [55.0, "..."],
    "sound_freq_int": [245.0, "..."],
    "sound_amp_int": [0.0042, "..."],
    "sound_freq_ext": [120.0, "..."],
    "sound_amp_ext": [0.0011, "..."],
    "light_ext": [7.6, "..."]
  }
}
```

---

## API server/ (port 5001)

### Authentification

Deux mécanismes coexistent :

| Type | Header | Utilisé par |
|---|---|---|
| JWT | `Authorization: Bearer <jwt>` | mobile/, app/ Pi (bridge) |
| API Key | `Authorization: Bearer <api_key>` | app/ Pi (push de données) |

Le serveur distingue les deux en vérifiant si le token est un JWT valide (3 segments base64 séparés par `.`) ou une clé API opaque.

---

### POST `/api/auth/login` — Connexion

**Corps** :

```json
{
  "username": "alice",
  "password": "secret"
}
```

**Réponse (200)** :

```json
{
  "token": "<JWT HS256>",
  "username": "alice",
  "role": "viewer",
  "expires_at": "2026-07-22T12:00:00+00:00"
}
```

**Réponse (401)** : `{"error": "Invalid credentials"}`

---

### POST `/api/auth/verify` — Vérification + JWT (pont Pi)

Vérifie les identifiants et retourne un JWT. Identique à `/api/auth/login` mais sémantiquement destiné au bridge d'authentification de l'app Pi.

**Corps** :

```json
{
  "username": "alice",
  "password": "secret"
}
```

**Réponse (200)** :

```json
{
  "valid": true,
  "token": "<JWT HS256>",
  "username": "alice",
  "role": "viewer",
  "expires_at": "2026-07-22T12:00:00+00:00"
}
```

**Réponse (401)** : `{"valid": false}`

---

### POST `/api/auth/logout` — Déconnexion

Les tokens JWT sont stateless — la déconnexion est gérée côté client (suppression du token stocké). Cet endpoint peut être appelé pour signaler la déconnexion ; le serveur retourne simplement `200 {"status": "ok"}`.

**Auth** : `Authorization: Bearer <jwt>`

---

### GET `/api/beehives` — Liste des ruches

**Auth** : `Authorization: Bearer <jwt>` ou `Bearer <api_key>`

**Réponse** :

```json
{
  "beehives": [
    {
      "id": "B001",
      "name": "Ruche du verger",
      "location": "12 rue des Tilleuls, Noisy-le-Grand",
      "source": "beetter-pi-01",
      "latest": {
        "temperature_int": {"value": 34.7, "time": "2026-05-19T14:30:00Z"},
        "humidity_int":    {"value": 62.1, "time": "2026-05-19T14:30:00Z"},
        "temperature_ext": {"value": 18.3, "time": "2026-05-19T14:30:00Z"},
        "humidity_ext":    {"value": 55.0, "time": "2026-05-19T14:30:00Z"},
        "sound_freq_int":  {"value": 245.0, "time": "2026-05-19T14:30:00Z"},
        "sound_amp_int":   {"value": 0.0042, "time": "2026-05-19T14:30:00Z"},
        "sound_freq_ext":  {"value": 120.0, "time": "2026-05-19T14:30:00Z"},
        "sound_amp_ext":   {"value": 0.0011, "time": "2026-05-19T14:30:00Z"},
        "light_ext":       {"value": 7.6, "time": "2026-05-19T14:30:00Z"}
      }
    }
  ]
}
```

---

### GET `/api/beehives/<id>/data` — Données de graphe

**Auth** : `Authorization: Bearer <jwt>` ou `Bearer <api_key>`

**Paramètres** : `?range=24h` (valeurs acceptées : `1h`, `6h`, `24h`, `7d`, `30d`)

**Réponse** :

```json
{
  "id": "B001",
  "range": "24h",
  "series": {
    "temperature_int": {
      "labels": ["2026-05-18T14:30:00Z", "..."],
      "data": [34.7, 34.8, "..."]
    },
    "humidity_int": {
      "labels": ["2026-05-18T14:30:00Z", "..."],
      "data": [62.1, 62.0, "..."]
    }
  }
}
```

---

### POST `/api/push` — Réception d'un lot depuis le Pi

Reçoit les données scalaires poussées par l'app Pi. Seuls les 9 scalaires sont inclus (pas les MFCC).

**Auth** : `Authorization: Bearer <api_key>`

**Corps** :

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
          "humidity_int": 62.1,
          "sound_freq_int": 245.0,
          "light_ext": 7.6
        }
      ]
    }
  ]
}
```

Chaque entrée de `data` peut contenir un sous-ensemble des 9 mesures scalaires. Les points sont écrits dans InfluxDB avec le tag `beehive_id` et un tag `source` identifiant l'instance Pi.

**Réponse** : `200 {"status": "ok", "points_written": 42}`

---

## Mesures InfluxDB

Noms canoniques des mesures, identiques dans InfluxDB, l'API et les trames LoRa :

| Mesure | Unité | Incluse dans push |
|---|---|---|
| `temperature_int` | °C | oui |
| `humidity_int` | % | oui |
| `temperature_ext` | °C | oui |
| `humidity_ext` | % | oui |
| `sound_freq_int` | Hz | oui |
| `sound_amp_int` | (relative) | oui |
| `sound_freq_ext` | Hz | oui |
| `sound_amp_ext` | (relative) | oui |
| `light_ext` | indice 0–10 | oui |
| `mfcc_int_0` … `mfcc_int_12` | — | **non** |
| `mfcc_ext_0` … `mfcc_ext_12` | — | **non** |

Chaque point InfluxDB a le schéma : `measurement=<nom>`, tag `beehive_id`, champ `value`.
