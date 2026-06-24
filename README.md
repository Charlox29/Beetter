<div align="center">
  <img src="https://beetter.fr/Logo_Beetter.svg" alt="Beetter" width="220"/>
  <br/><br/>

  [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
  [![Contributing](https://img.shields.io/badge/Contributing-guide-blue.svg)](CONTRIBUTING.md)
  [![Website](https://img.shields.io/badge/Website-beetter.fr-4CAF50.svg)](https://beetter.fr)
</div>

---

# Beetter — Système de surveillance de ruches

Beetter est une plateforme IoT de bout en bout pour la surveillance de ruches connectées. Les nœuds capteurs ESP32-C6 relèvent température, humidité, niveau sonore (MFCC) et luminosité, les transmettent par LoRa 868 MHz à un Raspberry Pi. Le Pi stocke les données, expose un dashboard web local et pousse périodiquement les mesures vers un serveur distant qui alimente l'application mobile Flutter.

```
┌─────────────────────────────────────────────────────────────────────┐
│  Firmware ESP32  (firmware/)                                        │
│  Nœud intérieur              Nœud extérieur                        │
│  (SHT40 + micro → MFCC)      (SHT40 + micro → MFCC + photorésistance)│
└──────────────────────┬──────────────────────────────────────────────┘
                       │  trames binaires LoRa (blocs ENV + AUD, 100 octets)
                       ▼
          ┌────────────────────────────┐
          │  Raspberry Pi              │
          │  • lora/receiver.py        │──── InfluxDB (local)
          │  • app/ Flask :5000        │──── InfluxDB + PostgreSQL (local)
          │  • IA/inference/worker     │
          │  • APScheduler (push)      │
          └────────────┬───────────────┘
                       │  POST /api/push  (Bearer API key)
                       ▼
          ┌────────────────────────────┐
          │  Serveur distant           │  Flask :5001
          │  • Dashboard admin         │──── InfluxDB + PostgreSQL (distant)
          │  • API REST mobile (JWT)   │
          └────────────┬───────────────┘
                       │  API REST  (Bearer JWT)
                       ▼
          Application mobile Flutter (Android + iOS)
```

---

## Capteurs

Chaque ruche porte **deux nœuds capteurs** (intérieur et extérieur). Le firmware calcule une **analyse MFCC** (13 coefficients) sur chaque microphone.

| Emplacement | Capteur | Grandeur mesurée | Champ API |
|---|---|---|---|
| Intérieur | SHT40 | Température | `temperature_int` |
| Intérieur | SHT40 | Humidité | `humidity_int` |
| Intérieur | Microphone | Fréquence dominante | `sound_freq_int` |
| Intérieur | Microphone | Amplitude RMS | `sound_amp_int` |
| Intérieur | Microphone | MFCC c0…c12 | `mfcc_int_0` … `mfcc_int_12` |
| Extérieur | SHT40 | Température | `temperature_ext` |
| Extérieur | SHT40 | Humidité | `humidity_ext` |
| Extérieur | Microphone | Fréquence dominante | `sound_freq_ext` |
| Extérieur | Microphone | Amplitude RMS | `sound_amp_ext` |
| Extérieur | Microphone | MFCC c0…c12 | `mfcc_ext_0` … `mfcc_ext_12` |
| Extérieur | Photorésistance | Luminosité (0–10) | `light_ext` |

Par ruche : **9 mesures scalaires** + **26 coefficients MFCC** = **35 mesures** au total.

---

## Démarrage rapide

### Application web (Raspberry Pi)

```bash
git clone https://github.com/Charlox29/Beetter.git ~/beetter
cd ~/beetter/app
cp .env.example .env          # renseigner SECRET_KEY et INFLUXDB_TOKEN
docker compose up -d --build
```

Ouvrir `http://<ip-du-pi>:5000` — le premier compte inscrit est admin.

### Mises à jour (Pi déjà déployé)

```bash
./deploy.sh            # git pull + venvs + rebuild Docker + restart services
./deploy.sh --no-build # rechargement rapide (templates/CSS uniquement)
```

> Pour le déploiement complet (LoRa, worker IA, services systemd, serveur distant, mobile), voir **[docs/DEPLOYMENT.md](docs/DEPLOYMENT.md)**.

---

## Documentation

| Document | Contenu |
|---|---|
| [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) | Déploiement complet — Docker, venvs Python, systemd, `deploy.sh` |
| [docs/APP.md](docs/APP.md) | Application web Pi — blueprints, modèles, scheduler, pipeline InfluxDB, seuils |
| [docs/SERVER.md](docs/SERVER.md) | Serveur distant — API REST, dashboard admin, JWT, Docker |
| [docs/MOBILE.md](docs/MOBILE.md) | Application mobile Flutter — architecture Riverpod, services, build |
| [docs/AUTH.md](docs/AUTH.md) | Architecture d'authentification JWT — pont Pi/serveur, flux mobile |
| [docs/API.md](docs/API.md) | Référence complète des endpoints REST (`app/` et `server/`) |
| [docs/INFRASTRUCTURE.md](docs/INFRASTRUCTURE.md) | Schémas infra, flux de données, schémas BDD, ports, variables d'env |
| [firmware/README.md](firmware/README.md) | Firmware ESP32 — brochage, MFCC, protocole LoRa, micro-SD |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Guide de développement — stack, setup local, Flask, InfluxDB, Android |

---

## Structure du projet

```
beetter/
├── deploy.sh               # Script de déploiement (git pull + venvs + Docker + systemd)
├── app/                    # Application web Raspberry Pi (Flask :5000, Docker)
├── server/                 # Serveur d'agrégation distant (Flask :5001, Docker)
├── mobile/                 # Application mobile Flutter (Android + iOS)
├── firmware/               # Firmware ESP32-C6 (Arduino/C++)
├── lora/                   # Récepteur LoRa Python (Grove RFM95 via UART)
│   └── .venv/              # Environnement virtuel Python (créé par deploy.sh)
├── IA/                     # Pipeline ML — classification d'état de ruche
│   ├── beehive/            # Modèle, entraînement, visualisation
│   ├── models/             # Modèles entraînés (.pkl)
│   └── inference/          # Worker d'inférence (service systemd)
│       └── .venv/          # Environnement virtuel Python (créé par deploy.sh)
├── tools/
│   └── simulate.py         # Simulateur de données capteurs (développement)
├── website/                # Site vitrine beetter.fr
└── docs/                   # Documentation technique détaillée
```
