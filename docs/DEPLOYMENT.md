# Guide de déploiement

Ce document couvre le déploiement complet de Beetter : application web Pi, serveur distant, récepteur LoRa, worker d'inférence IA et application mobile Flutter.

---

## Prérequis

### Raspberry Pi

| Composant | Version minimale |
|---|---|
| Raspberry Pi | 3B+ ou supérieur (4 recommandé) |
| Raspberry Pi OS | Bullseye 64-bit ou supérieur |
| Docker Engine | 24+ |
| Docker Compose | V2 (plugin `docker compose`) |
| Python | 3.11+ |
| RAM libre | ≥ 512 MB |
| Stockage | ≥ 8 GB (carte SD classe A2 recommandée) |

### Serveur distant

| Composant | Version minimale |
|---|---|
| OS | Linux (Ubuntu 22.04 LTS recommandé) |
| Docker Engine | 24+ |
| Docker Compose | V2 |
| RAM | ≥ 1 GB |
| Ports ouverts | 5001 (app), 8087 (InfluxDB — optionnel, usage interne) |

---

## 1. Application web Pi (`app/`)

L'application Flask tourne entièrement dans Docker — aucun environnement Python local n'est requis pour ce composant.

### Cloner le dépôt

```bash
git clone https://github.com/Charlox29/Beetter.git ~/beetter
cd ~/beetter
```

### Configurer l'environnement

```bash
cd app
cp .env.example .env
nano .env
```

Valeurs minimales à renseigner :

```env
SECRET_KEY=<chaîne aléatoire longue>
INFLUXDB_TOKEN=<token-à-generer>
```

Générer un `SECRET_KEY` fort :
```bash
python3 -c "import secrets; print(secrets.token_hex(32))"
```

### Démarrer les conteneurs

```bash
cd app
docker compose up -d --build
```

Première exécution : Docker télécharge les images (`python:3.11-slim`, `postgres:16-alpine`, `influxdb:2.7-alpine`). Compter 5–10 minutes sur une connexion standard.

Vérification :
```bash
docker compose ps          # tous les services doivent être "healthy" / "running"
docker compose logs app    # vérifier le démarrage Gunicorn
```

Ouvrir `http://<ip-du-pi>:5000`. **Le premier compte inscrit est automatiquement admin.**

### Créer les ruches

Avant d'utiliser le simulateur ou le récepteur LoRa, créer au moins une ruche dans l'interface : **Ruches → Nouvelle ruche**.

### Connecter au serveur distant (optionnel)

1. **Paramètres → Ajouter un serveur** : renseigner l'URL et l'API key du `server/`.
2. **Paramètres → Pont de compte distant** : renseigner l'URL et le `JWT_SECRET_KEY`.

---

## 2. Environnements virtuels Python

Les composants Python qui tournent directement sur l'hôte (hors Docker) utilisent des environnements virtuels isolés pour éviter les conflits de dépendances avec le système. Les répertoires `.venv/` sont créés à l'intérieur de chaque composant et exclus du contrôle de version.

> Le script `deploy.sh` crée et met à jour ces venvs automatiquement lors de chaque déploiement. Cette section décrit les étapes manuelles pour un premier déploiement ou en cas de problème.

### Récepteur LoRa (`lora/`)

```bash
cd ~/beetter/lora
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
deactivate
```

Vérification :
```bash
~/beetter/lora/.venv/bin/python3 -c "import serial, requests; print('OK')"
```

### Worker d'inférence IA (`IA/inference/`)

```bash
cd ~/beetter/IA/inference
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
deactivate
```

Vérification :
```bash
~/beetter/IA/inference/.venv/bin/python3 -c "import torch, influxdb_client; print('OK')"
```

---

## 3. Récepteur LoRa (`lora/`)

Le récepteur tourne hors Docker, directement sur le Pi, car il doit accéder au port série physique.

### Activation du port série (une seule fois)

```bash
sudo raspi-config
# Interface Options → Serial Port → No shell / Yes hardware serial
sudo reboot
```

Vérifier le port disponible :
```bash
ls /dev/serial*   # → /dev/serial0 (RPi 4) ou /dev/ttyAMA10 (RPi 5)
```

Si nécessaire, adapter la variable `PORT` dans `lora/receiver.py`.

### Lancement manuel

```bash
# Mode console uniquement (sans envoi à l'API Flask)
~/beetter/lora/.venv/bin/python3 ~/beetter/lora/receiver.py

# Avec envoi vers l'API Flask
API_ENABLE=1 BEETTER_API_URL=http://localhost:5000 \
  ~/beetter/lora/.venv/bin/python3 ~/beetter/lora/receiver.py

# Utiliser l'horodatage embarqué dans la trame plutôt que l'heure du Pi
API_ENABLE=1 USE_LORA_TIMESTAMP=1 \
  ~/beetter/lora/.venv/bin/python3 ~/beetter/lora/receiver.py
```

### Service systemd

Créer le fichier `/etc/systemd/system/beetter-lora.service` :

```ini
[Unit]
Description=Beetter LoRa Receiver
After=network.target docker.service
Requires=docker.service

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/beetter/lora
ExecStart=/home/pi/beetter/lora/.venv/bin/python3 /home/pi/beetter/lora/receiver.py
Environment=API_ENABLE=1
Environment=BEETTER_API_URL=http://localhost:5000
Environment=USE_LORA_TIMESTAMP=0
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

> **Important** : le chemin de l'interpréteur pointe vers `.venv/bin/python3`, non vers `/usr/bin/python3`. Cela garantit que les dépendances de `lora/requirements.txt` sont disponibles indépendamment de l'état du système.

Activer et démarrer le service :

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now beetter-lora
sudo systemctl status beetter-lora    # vérifier que le service tourne
journalctl -u beetter-lora -f         # logs en temps réel
```

---

## 4. Worker d'inférence IA (`IA/inference/`)

Le worker analyse en continu les données InfluxDB pour détecter des événements de ruche (essaimage, frelon, perte de reine) à l'aide des modèles entraînés dans `IA/models/`.

### Lancement manuel

```bash
~/beetter/IA/inference/.venv/bin/python3 ~/beetter/IA/inference/inference_worker.py
```

### Service systemd

Créer le fichier `/etc/systemd/system/beetter-inference.service` :

```ini
[Unit]
Description=Beetter Inference Worker
After=network.target docker.service
Requires=docker.service

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/beetter/IA/inference
ExecStart=/home/pi/beetter/IA/inference/.venv/bin/python3 /home/pi/beetter/IA/inference/inference_worker.py
Restart=on-failure
RestartSec=30

[Install]
WantedBy=multi-user.target
```

Activer et démarrer le service :

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now beetter-inference
sudo systemctl status beetter-inference
journalctl -u beetter-inference -f
```

---

## 5. Script de déploiement (`deploy.sh`)

Pour les **mises à jour** sur un Pi déjà déployé, utiliser `deploy.sh` à la racine du dépôt. Il automatise : git pull, création/mise à jour des venvs Python, rebuild Docker, health-check Flask, restart des services systemd.

```bash
./deploy.sh                        # branche main, rebuild complet
./deploy.sh --no-build             # redémarre sans rebuild (templates/CSS/JS seulement)
./deploy.sh --branch feature/xyz   # déploie une branche spécifique
./deploy.sh --force                # écrase les modifications locales sans demander
```

Le script vérifie la présence de `app/.env` et interrompt si manquant.

### Contenu de référence

```bash
#!/bin/bash
# deploy.sh — Script de déploiement Beetter (Raspberry Pi)
set -euo pipefail

# ── Couleurs ─────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; NC='\033[0m'
log()     { echo -e "${CYAN}[deploy]${NC} $1"; }
success() { echo -e "${GREEN}[deploy]${NC} ✓ $1"; }
warn()    { echo -e "${YELLOW}[deploy]${NC} ⚠ $1"; }
error()   { echo -e "${RED}[deploy]${NC} ✗ $1"; exit 1; }

# ── Paramètres ───────────────────────────────────────────────────────────────
BRANCH="main"
DO_BUILD=true
FORCE=false
REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$REPO_DIR/app"
LORA_SERVICE="beetter-lora"
INFERENCE_SERVICE="beetter-inference"

while [[ $# -gt 0 ]]; do
  case $1 in
    --no-build) DO_BUILD=false; shift ;;
    --branch)   BRANCH="$2"; shift 2 ;;
    --force)    FORCE=true; shift ;;
    -h|--help)
      echo "Usage: $0 [--no-build] [--branch <nom>] [--force]"; exit 0 ;;
    *) error "Argument inconnu : $1" ;;
  esac
done

log "Déploiement Beetter — branche : ${BRANCH}"
echo "────────────────────────────────────────────"

# ── 1. Git pull ───────────────────────────────────────────────────────────────
log "Récupération des modifications (git fetch)..."
git fetch origin

if [[ -n "$(git status --porcelain)" ]]; then
  warn "Modifications locales détectées sur le Pi :"
  git status --short
  if [[ "$FORCE" != true ]]; then
    if [[ ! -t 0 ]]; then
      error "Session non interactive : relance avec --force ou commit/stash avant de redéployer."
    fi
    read -rp "Les écraser (git reset --hard) pour repartir sur origin/${BRANCH} ? [y/N] " reponse
    [[ "$reponse" =~ ^[Yy]$ ]] || error "Déploiement annulé."
  fi
  git reset --hard HEAD
fi

git checkout "$BRANCH"
git pull origin "$BRANCH"
success "Code à jour ($(git rev-parse --short HEAD))"

# ── 2. Vérification du .env ──────────────────────────────────────────────────
if [[ ! -f "$APP_DIR/.env" ]]; then
  warn ".env manquant dans app/ — copie de .env.example"
  cp "$APP_DIR/.env.example" "$APP_DIR/.env"
  warn "Edite $APP_DIR/.env avant de relancer le script !"
  exit 1
fi
success ".env présent"

# ── 3. Environnements virtuels Python ────────────────────────────────────────
log "Mise à jour des environnements virtuels Python..."

if [[ ! -d "$REPO_DIR/lora/.venv" ]]; then
  log "Création de lora/.venv..."
  python3 -m venv "$REPO_DIR/lora/.venv"
fi
"$REPO_DIR/lora/.venv/bin/pip" install -q --upgrade pip
"$REPO_DIR/lora/.venv/bin/pip" install -q -r "$REPO_DIR/lora/requirements.txt"
success "lora/.venv OK"

if [[ ! -d "$REPO_DIR/IA/inference/.venv" ]]; then
  log "Création de IA/inference/.venv..."
  python3 -m venv "$REPO_DIR/IA/inference/.venv"
fi
"$REPO_DIR/IA/inference/.venv/bin/pip" install -q --upgrade pip
"$REPO_DIR/IA/inference/.venv/bin/pip" install -q -r "$REPO_DIR/IA/inference/requirements.txt"
success "IA/inference/.venv OK"

# ── 4. Docker Compose ────────────────────────────────────────────────────────
cd "$APP_DIR"

if [[ "$DO_BUILD" == true ]]; then
  log "Build et redémarrage des conteneurs Docker..."
  docker compose up -d --build --remove-orphans
else
  log "Redémarrage sans rebuild (--no-build)..."
  docker compose up -d --remove-orphans
fi

# ── 5. Health check Flask ─────────────────────────────────────────────────────
log "Attente du démarrage de Flask..."
for i in {1..15}; do
  if curl -sf http://localhost:5000 > /dev/null 2>&1; then
    success "Flask répond sur :5000"; break
  fi
  sleep 2
  [[ $i -eq 15 ]] && error "Flask ne répond pas après 30s — vérifie : docker compose logs"
done

# ── 6. Redémarrage des services systemd ──────────────────────────────────────
if systemctl is-active --quiet "$LORA_SERVICE" 2>/dev/null; then
  log "Redémarrage du service LoRa..."
  sudo systemctl restart "$LORA_SERVICE"
  success "Service ${LORA_SERVICE} redémarré"
else
  warn "Service ${LORA_SERVICE} non trouvé ou inactif — ignoré"
fi

if systemctl is-active --quiet "$INFERENCE_SERVICE" 2>/dev/null; then
  log "Redémarrage du worker d'inférence..."
  sudo systemctl restart "$INFERENCE_SERVICE"
  success "Service ${INFERENCE_SERVICE} redémarré"
else
  warn "Service ${INFERENCE_SERVICE} non trouvé ou inactif — ignoré"
fi

# ── 7. Résumé final ────────────────────────────────────────────────────────
echo ""
echo "────────────────────────────────────────────"
success "Déploiement terminé !"
log "Dashboard local : http://$(hostname -I | awk '{print $1}'):5000"
log "Commit déployé  : $(git log -1 --format='%h — %s')"
log "Logs            : docker compose logs -f  (depuis app/)"
```

---

## 6. Serveur distant (`server/`)

### Configurer l'environnement

```bash
cd server
cp .env.example .env
nano .env
```

```env
SECRET_KEY=<chaîne différente de celle de l'app/>
JWT_SECRET_KEY=<secret partagé avec les Pi liés>
INFLUXDB_TOKEN=<token-à-generer>
```

### Démarrer les conteneurs

```bash
cd server
docker compose up -d --build
```

Le serveur écoute sur le port **5001**. Ouvrir `http://<ip-serveur>:5001`. Le premier compte inscrit est admin.

### Générer une API key

**Dashboard → API Keys → New key**. Copier la clé immédiatement (affichée une seule fois). Renseigner dans l'app Pi : **Paramètres → Ajouter un serveur**.

---

## 7. Mode kiosk (Raspberry Pi — optionnel)

Pour ouvrir automatiquement le dashboard en plein écran au démarrage du Pi.

### Script de lancement

Créer `~/start-dashboard.sh` :
```bash
#!/bin/bash
until curl -s -o /dev/null http://localhost:5000; do sleep 1; done
firefox --kiosk http://localhost:5000
```
```bash
chmod +x ~/start-dashboard.sh
```

### Autostart

Créer `~/.config/autostart/beetter-dashboard.desktop` :
```ini
[Desktop Entry]
Type=Application
Name=Beetter Dashboard
Exec=/home/pi/start-dashboard.sh
X-GNOME-Autostart-enabled=true
```

### Autologin

```bash
sudo raspi-config
# System Options → Boot / Auto Login → Desktop Autologin
```

Pour désactiver temporairement :
```bash
mv ~/.config/autostart/beetter-dashboard.desktop{,.disabled}
```

| Action | Raccourci |
|---|---|
| Fermer Firefox | `Alt+F4` |
| Basculer sur un terminal | `Ctrl+Alt+F2` |
| Revenir à l'interface graphique | `Ctrl+Alt+F1` |

---

## 8. Application mobile

### Android

```bash
cd mobile
flutter pub get
flutter build apk --release
adb install build/app/outputs/flutter-apk/app-release.apk
```

### Android (debug rapide)

```bash
flutter run   # sur appareil connecté ou émulateur AVD
```

### iOS (macOS + Xcode 15+)

```bash
cd mobile
flutter pub get
flutter build ios --release
# Archiver dans Xcode → Product → Archive
```

---

## 9. Réseau et firewall

### Raspberry Pi

| Port | Service | Accessible depuis |
|---|---|---|
| 5000 | App Flask (Gunicorn) | Réseau local |
| 8086 | InfluxDB | Interne Docker uniquement |
| 5432 | PostgreSQL | Interne Docker uniquement |

### Serveur distant

| Port | Service | Accessible depuis |
|---|---|---|
| 5001 | Server Flask (Gunicorn) | Internet (app mobile + Pi) |
| 8087 | InfluxDB (port externe) | Optionnel — usage interne uniquement |
| 5432 | PostgreSQL | Interne Docker uniquement |

**Recommandation production** : placer un reverse proxy (Nginx ou Caddy) devant le port 5001 pour gérer HTTPS. Les ports PostgreSQL et InfluxDB ne doivent jamais être exposés publiquement.

### Exemple Nginx (HTTPS)

```nginx
server {
    listen 443 ssl http2;
    server_name beetter.example.com;

    ssl_certificate     /etc/letsencrypt/live/beetter.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/beetter.example.com/privkey.pem;

    location / {
        proxy_pass         http://127.0.0.1:5001;
        proxy_set_header   Host $host;
        proxy_set_header   X-Real-IP $remote_addr;
        proxy_set_header   X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header   X-Forwarded-Proto $scheme;
        proxy_read_timeout 120s;
    }
}

server {
    listen 80;
    server_name beetter.example.com;
    return 301 https://$host$request_uri;
}
```

---

## 10. Sauvegardes

### PostgreSQL

```bash
# app/
docker compose exec db pg_dump -U beetter beetter > backup_app_$(date +%F).sql

# server/
docker compose exec db pg_dump -U beetter_srv beetter_srv > backup_server_$(date +%F).sql
```

### InfluxDB

```bash
docker compose exec influxdb influx backup /tmp/backup
docker compose cp influxdb:/tmp/backup ./influx-backup-$(date +%F)
```

---

## 11. Surveillance et logs

```bash
# Logs Flask en temps réel
cd app && docker compose logs -f app

# Logs récepteur LoRa
journalctl -u beetter-lora -f

# Logs worker d'inférence
journalctl -u beetter-inference -f

# État des conteneurs
docker compose ps

# Utilisation ressources Pi
docker stats
```

---

## 12. Dépannage fréquent

| Symptôme | Cause probable | Solution |
|---|---|---|
| `beetter-lora` crash immédiatement | Venv absent ou incomplet | `cd lora && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt` |
| `beetter-inference` crash immédiatement | Venv absent ou incomplet | `cd IA/inference && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt` |
| `ModuleNotFoundError` dans receiver.py | Dépendances hors du venv | Vérifier que le service systemd pointe vers `.venv/bin/python3` |
| Dashboard vide (—) | InfluxDB non initialisé ou token incorrect | Vérifier `INFLUXDB_TOKEN` dans `app/.env` |
| Push `offline` en permanence | URL serveur incorrecte ou port bloqué | Tester `curl {url}/api/push` depuis le Pi |
| Récepteur LoRa : aucune trame | Port série incorrect ou module débranché | Vérifier `ls /dev/serial*` et câblage |
| Connexion mobile échoue | URL ou credentials incorrects | Vérifier que le serveur répond sur le port 5001 |
| Conteneurs ne démarrent pas | `.env` manquant | Copier `.env.example` et renseigner les secrets |
| Flask répond 500 | Migrations non appliquées | `docker compose exec app flask db upgrade` |
