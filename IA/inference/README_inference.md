# Inference Worker — Documentation

## Vue d'ensemble

Le module `IA/inference/inference_worker.py` est un processus autonome qui tourne en parallèle de `receiver.py` sur le Raspberry Pi. Il ne modifie aucun fichier existant du pipeline.

```
receiver.py  →  bucket "sensors"  (inchangé)
                       ↓
          inference_worker.py  (nouveau processus indépendant)
                       ↓
              bucket "anomaly"   (résultats IA)
```

À chaque cycle, le worker :
1. Interroge le bucket `sensors` pour récupérer les dernières mesures
2. Les passe dans le modèle de classification (normal / anomalie)
3. Écrit les probabilités et l'état détecté dans le bucket `anomaly`

Les buckets `anomaly` et `predictions` sont **créés automatiquement** au premier lancement si ils n'existent pas encore dans InfluxDB.

---

## Prérequis

Installer les dépendances depuis `IA/inference/` :

```bash
pip install -r requirements_inference.txt --break-system-packages
```

> **Raspberry Pi — PyTorch CPU**
> PyTorch nécessite une installation spécifique sur architecture ARM :
> ```bash
> pip install torch --index-url https://download.pytorch.org/whl/cpu --break-system-packages
> ```

---

## Fichiers nécessaires

Avant de lancer le worker, s'assurer que les fichiers suivants sont présents :

| Fichier | Description |
|---|---|
| `IA/checkpoints/finetune_best.pt` | Modèle fine-tuné (généré par `fit_and_train.py --mode finetune`) |
| `IA/calibration/norm_in.json` | Normalizer capteur intérieur (généré par le pré-entraînement) |
| `IA/calibration/norm_out.json` | Normalizer capteur extérieur (généré par le pré-entraînement) |
| `app/.env` | Credentials InfluxDB (`INFLUXDB_URL`, `INFLUXDB_TOKEN`, `INFLUXDB_ORG`) |

---

## Lancement

Depuis `Beetter/IA/inference/` :

```bash
python inference_worker.py
```

Avec options :

```bash
python inference_worker.py \
    --hive-id B001 \
    --interval 30 \
    --lookback 120
```

| Argument | Défaut | Description |
|---|---|---|
| `--hive-id` | `B001` | Identifiant de la ruche à surveiller |
| `--interval` | `30` | Intervalle de polling en secondes |
| `--lookback` | `120` | Fenêtre de requête InfluxDB en secondes |
| `--checkpoint` | `../checkpoints/finetune_best.pt` | Chemin vers le modèle |
| `--norm-in` | `../calibration/norm_in.json` | Chemin vers le normalizer intérieur |
| `--norm-out` | `../calibration/norm_out.json` | Chemin vers le normalizer extérieur |
| `--env` | `../../app/.env` | Chemin vers le fichier .env |

---

## Sortie InfluxDB

Chaque mesure traitée génère un point dans le bucket `anomaly` :

| Champ | Type | Description |
|---|---|---|
| `p_normal` | float | Probabilité état normal (0.0 – 1.0) |
| `p_anomaly` | float | Probabilité état anormal (0.0 – 1.0) |
| `label` | string | Classe prédite (`"normal"` ou `"anomaly"`) |
| `alert` | bool | `true` si anomalie détectée sur 3 relevés consécutifs |

Tag : `hive_id` — permet de filtrer par ruche dans Grafana / InfluxDB.

---

## Logique d'alerte

Une alerte est déclenchée uniquement si `p_anomaly > 0.40` pendant **3 relevés consécutifs** (soit ~1m30 à intervalle de 30s). Ce mécanisme évite les fausses alarmes dues à des pics ponctuels.

---

## Lancer en arrière-plan (production)

Pour que le worker tourne en continu sur le Raspberry Pi sans bloquer le terminal :

```bash
nohup python inference_worker.py --hive-id B001 > inference.log 2>&1 &
```

Pour l'arrêter :

```bash
pkill -f inference_worker.py
```

---

## Repasser à 6 classes (futur)

Le worker est compatible avec le modèle 6 classes sans modification. Il suffit de :
1. Décommenter les 5 états dans `beehive/config.py`
2. Relancer `fit_and_train.py --mode finetune` avec des données étiquetées sur 6 classes
3. Relancer `inference_worker.py` — il détectera automatiquement le nouveau nombre de classes
