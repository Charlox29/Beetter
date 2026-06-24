# Guide de déploiement

Ce document couvre le déploiement complet de Beetter : Raspberry Pi (app/), serveur distant (server/), récepteur LoRa, et l'application mobile Flutter.

---

## Prérequis

### Raspberry Pi

| Composant | Version minimale |
|---|---|
| Raspberry Pi | 3B+ ou supérieur (4 recommandé) |
| Raspberry Pi OS | Bullseye 64-bit ou supérieur |
| Docker Engine | 24+ |
| Docker Compose | V2 (plugin `docker compose`) |
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

## Déploiement — Application Pi (`app/`)

### 1. Cloner le dépôt

```bash
git clone https://github.com/<org>/beetter.git ~/beetter
cd ~/beetter
```

### 2. Configurer l'environnement

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

Les autres valeurs sont pré-remplies pour Docker Compose (URLs internes, noms de bases, etc.).

Générer un `SECRET_KEY` fort :
```bash
python3 -c "import secrets; print(secrets.token_hex(32))"
```

### 3. Démarrer les conteneurs

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

### 4. Premier lancement

Ouvre `http://<ip-du-pi>:5000`. La page d'inscription s'affiche. **Le premier compte inscrit est automatiquement admin.**

### 5. Créer les ruches

Avant d'utiliser le simulateur ou le récepteur LoRa, crée au moins une ruche dans l'interface : **Ruches → Nouvelle ruche**.

### 6. Connecter au serveur distant (optionnel)

1. **Paramètres → Ajouter un serveur** : renseigne l'URL et l'API key du server/.
2. **Paramètres → Pont de compte distant** : renseigne l'URL et le `JWT_SECRET_KEY`.

---

## Déploiement — Serveur distant (`server/`)

### 1. Configurer l'environnement

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

### 2. Démarrer les conteneurs

```bash
cd server
docker compose up -d --build
```

Le serveur écoute sur le port `5001`. L'InfluxDB du serveur écoute sur le port externe `8087`.

### 3. Premier lancement

Ouvre `http://<ip-serveur>:5001`. Inscris un compte — le premier est admin.

### 4. Générer une API key

**Dashboard → API Keys → New key**. Copie la clé immédiatement (affichée une seule fois). Renseigne-la dans l'app Pi (**Paramètres → Ajouter un serveur**).

---

## Récepteur LoRa (`lora/`)

Le récepteur tourne hors Docker, directement sur le Pi, car il doit accéder au port série physique.

### Installation

```bash
cd ~/beetter/lora
pip3 install -r requirements.txt
```

### Lancement manuel

```bash
# Mode console uniquement (sans envoi à l'API Flask)
python3 receiver.py

# Avec envoi vers l'API Flask
API_ENABLE=1 BEETTER_API_URL=http://localhost:5000 python3 receiver.py

# Utiliser l'horodatage de la trame LoRa au lieu du Pi
API_ENABLE=1 USE_LORA_TIMESTAMP=1 python3 receiver.py
```

### Service systemd

Crée le fichier `/etc/systemd/system/beetter-lora.service` :

```ini
[Unit]
Description=Beetter LoRa Receiver
After=network.target docker.service
Requires=docker.service

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/beetter/lora
ExecStart=/usr/bin/python3 /home/pi/beetter/lora/receiver.py
Environment=API_ENABLE=1
Environment=BEETTER_API_URL=http://localhost:5000
Environment=USE_LORA_TIMESTAMP=0
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now beetter-lora
sudo systemctl status beetter-lora    # vérifie que le service tourne
journalctl -u beetter-lora -f         # logs en temps réel
```

**Prérequis matériel** : module Grove LoRa 868 MHz connecté via UART sur `/dev/serial0`. Activer le port série :
```bash
sudo raspi-config
# Interface Options → Serial Port → No shell / Yes hardware serial
sudo reboot
```

---

## Script de déploiement (`deploy.sh`)

Pour les **mises à jour** sur un Pi déjà déployé, utilise `deploy.sh` à la racine du dépôt. Il automatise : git pull, rebuild Docker, health-check Flask, restart service LoRa.

```bash
./deploy.sh                        # branche main, rebuild complet
./deploy.sh --no-build             # redémarre sans rebuild (templates/CSS/JS seulement)
./deploy.sh --branch feature/xyz   # déploie une branche spécifique
./deploy.sh --force                # écrase les modifications locales sans demander
```

Le script vérifie la présence de `app/.env` et interrompt si manquant.

---

## Mode kiosk (Raspberry Pi — optionnel)

Pour ouvrir automatiquement le dashboard au démarrage en plein écran.

### 1. Script de lancement

Crée `~/start-dashboard.sh` :
```bash
#!/bin/bash
until curl -s -o /dev/null http://localhost:5000; do sleep 1; done
firefox --kiosk http://localhost:5000
```
```bash
chmod +x ~/start-dashboard.sh
```

### 2. Autostart

Crée `~/.config/autostart/beetter-dashboard.desktop` :
```ini
[Desktop Entry]
Type=Application
Name=Beetter Dashboard
Exec=/home/pi/start-dashboard.sh
X-GNOME-Autostart-enabled=true
```

### 3. Autologin

```bash
sudo raspi-config
# System Options → Boot / Auto Login → Desktop Autologin
```

Pour désactiver temporairement :
```bash
mv ~/.config/autostart/beetter-dashboard.desktop{,.disabled}
```

---

## Application mobile

### Android (depuis la ligne de commande)

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
# Puis archiver dans Xcode → Product → Archive
```

---

## Réseau et firewall

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

**Recommandation production** : placer un reverse proxy (Nginx ou Caddy) devant le port 5001 pour gérer HTTPS et les en-têtes de sécurité. Les ports PostgreSQL et InfluxDB ne doivent jamais être exposés publiquement.

### Exemple Nginx (serveur distant, HTTPS)

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

## Sauvegardes

### PostgreSQL

```bash
# app/
docker compose exec db pg_dump -U beetter beetter > backup_app_$(date +%F).sql

# server/
docker compose exec db pg_dump -U beetter_srv beetter_srv > backup_server_$(date +%F).sql
```

### InfluxDB

```bash
# Depuis le conteneur
docker compose exec influxdb influx backup /tmp/backup
docker compose cp influxdb:/tmp/backup ./influx-backup-$(date +%F)
```

---

## Surveillance et logs

```bash
# Logs en temps réel (app Pi)
cd app && docker compose logs -f app

# Logs LoRa
journalctl -u beetter-lora -f

# Santé des conteneurs
docker compose ps

# Utilisation ressources Pi
docker stats

# Espace disque InfluxDB
docker compose exec influxdb influx bucket list
```

---

## Dépannage fréquent

| Symptôme | Cause probable | Solution |
|---|---|---|
| Dashboard vide (—) | InfluxDB non initialisé ou token incorrect | Vérifier `INFLUXDB_TOKEN` dans `.env` |
| Push `offline` en permanence | URL serveur incorrecte ou port bloqué | Tester `curl {url}/api/push` depuis le Pi |
| Récepteur LoRa : aucune trame | Port série incorrect ou module débranché | Vérifier `ls /dev/serial*` et câblage |
| Connexion mobile échoue | JWT_SECRET_KEY différent entre server/ et mobile | Vérifier que le serveur reçoit bien les requêtes et que l'URL est correcte |
| Conteneurs ne démarrent pas | `.env` manquant | Copier `.env.example` et renseigner les secrets |
| Flask répond 500 | Migrations non appliquées | `docker compose exec app flask db upgrade` ou vérifier les logs |
