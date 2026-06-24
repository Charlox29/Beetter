# Authentification centralisée — Architecture JWT (Option A)

## Vue d'ensemble

Beetter utilise un modèle d'authentification **JWT HS256** dans lequel le serveur distant (`server/`) est le fournisseur d'identité. Les trois plateformes (app Pi, mobile, serveur) partagent ce mécanisme :

```
server/         →  émet les JWT (30 jours)
mobile/         →  s'authentifie directement sur server/
app/ (Pi)       →  délègue à server/ quand online, utilise le cache local hors ligne
```

---

## Tokens JWT

**Algorithme** : HS256  
**Durée de vie** : 30 jours  
**Secret** : variable d'environnement `JWT_SECRET_KEY` sur `server/`

**Payload** :

```json
{
  "sub": "alice",
  "role": "admin",
  "iat": 1750000000,
  "exp": 1752592000
}
```

Les tokens sont **stateless** — aucun stockage côté serveur. La révocation se fait en changeant `JWT_SECRET_KEY` (invalide tous les tokens existants).

---

## Flux mobile (direct)

```
mobile/ ──POST /api/auth/login──▶ server/
         { username, password }
                              ◀── { token: "<JWT>", username, role, expires_at }

mobile/ ──GET /api/beehives──▶ server/
         Authorization: Bearer <JWT>
                              ◀── { beehives: [...] }
```

Le token est stocké via `flutter_secure_storage` (chiffrement natif Android Keystore / iOS Keychain).

---

## Flux app/ Pi — pont JWT (bridge)

Ce mécanisme permet aux utilisateurs de se connecter sur le Pi avec leurs identifiants du serveur central, **même sans internet**.

### Premier login (online)

```
1. Utilisateur entre ses identifiants sur app/ (Pi)
2. app/ cherche le compte en local (PostgreSQL) → non trouvé
3. app/ appelle POST /api/auth/verify sur server/
   { username: "alice", password: "secret" }
4. server/ vérifie les identifiants, émet un JWT signé
   { valid: true, token: "<JWT>", username: "alice", role: "viewer", expires_at: "..." }
5. app/ vérifie la signature du JWT avec JWT_SECRET_KEY partagé
6. app/ crée/met à jour le compte local (User) avec username, role, password hashé
7. Flask-Login connecte l'utilisateur
```

### Logins suivants (online ou offline)

```
1. Utilisateur entre ses identifiants sur app/ (Pi)
2. app/ trouve le compte en local → vérifie le mot de passe hashé
3. Connexion locale immédiate, sans réseau
```

La résolution locale est toujours tentée **en premier** : les comptes locaux purs (sans lien avec le serveur) fonctionnent toujours, même si un serveur est configuré.

---

## Configuration

### Côté serveur (`server/.env`)

```env
JWT_SECRET_KEY=change-me-long-random-secret
```

Générer un secret fort :
```bash
python3 -c "import secrets; print(secrets.token_hex(32))"
```

### Côté app/ Pi (interface web)

1. Connecte-toi à l'app Pi en tant qu'admin.
2. **Paramètres → Pont de compte distant**.
3. Renseigne :
   - **URL du serveur** : `http://192.168.1.10:5001`
   - **JWT Secret** : valeur de `JWT_SECRET_KEY` du serveur
4. Sauvegarde.

Ces valeurs sont stockées dans la table `SystemConfig` (clés `linked_server_url` et `linked_server_jwt_secret`).

### Côté mobile

L'URL du serveur et les identifiants sont saisis à l'écran de connexion. Aucune configuration additionnelle n'est nécessaire.

---

## Comptes locaux (instances hors ligne)

Si le Pi n'est pas connecté à internet et qu'aucun serveur n'est configuré, les utilisateurs peuvent s'inscrire directement via `/register` sur l'app Pi. Ces comptes sont **purement locaux** et ne sont jamais synchronisés avec le serveur.

Le bridge JWT ne s'active que lorsque :
- Un serveur est configuré (`linked_server_url` non vide), **ET**
- Le nom d'utilisateur n'existe pas encore en local.

---

## Endpoint `/api/auth/verify`

Utilisé exclusivement par le bridge app/ Pi.

**Requête** :

```http
POST /api/auth/verify
Content-Type: application/json

{
  "username": "alice",
  "password": "secret"
}
```

**Réponse (succès)** :

```json
{
  "valid": true,
  "token": "<JWT HS256>",
  "username": "alice",
  "role": "viewer",
  "expires_at": "2026-07-22T12:00:00+00:00"
}
```

**Réponse (échec)** :

```json
{
  "valid": false
}
```

HTTP 401 si les identifiants sont incorrects.

---

## Considérations de sécurité

- `JWT_SECRET_KEY` doit être un secret aléatoire **long et unique**, différent de `SECRET_KEY`.
- Il est **partagé** entre server/ et les instances app/ qui lui sont liées : le conserver confidentiel est crucial.
- Les tokens n'ont pas de mécanisme de révocation individuelle. Pour invalider tous les tokens (ex. compromission), changer `JWT_SECRET_KEY` suffit.
- Le transport doit utiliser HTTPS en production pour protéger les tokens et les mots de passe en transit.
- Sur app/ Pi, le secret JWT est stocké dans PostgreSQL (table `SystemConfig`). L'accès à la base de données doit être protégé.
