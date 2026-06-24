"""
IA/inference/inference_worker.py
─────────────────────────────────────────────────────────────────────────────
Standalone inference worker for the Beetter project.

Runs as an independent process on the Raspberry Pi alongside receiver.py.
Does NOT modify receiver.py or any existing pipeline.

What it does every --interval seconds:
  1. Reads the latest unprocessed rows from the 'sensors' InfluxDB bucket
  2. Runs them through the fine-tuned beehive model
  3. Writes p_normal, p_anomaly, label, alert to the 'anomaly' InfluxDB bucket

On first run, automatically creates the 'anomaly' and 'predictions' buckets
if they do not already exist.

Usage (from Beetter/IA/inference/):
  python inference_worker.py

Optional flags:
  --interval     30          Polling interval in seconds (default: 30)
  --hive-id      B001        Hive ID tag to filter from sensors bucket
  --env          ../../app/.env   Path to the .env file with InfluxDB credentials
  --checkpoint   ../checkpoints/finetune_best.pt
  --norm-in      ../calibration/norm_in.json
  --norm-out     ../calibration/norm_out.json
  --lookback     120         How many seconds back to query on each poll (default: 120)
"""

import argparse
import logging
import sys
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path

# ── Make beehive package importable ──────────────────────────────────────────
_IA_DIR = Path(__file__).parent.parent
sys.path.insert(0, str(_IA_DIR))

import numpy as np
import torch
from dotenv import load_dotenv
from influxdb_client import InfluxDBClient, Point, BucketsApi
from influxdb_client.client.write_api import SYNCHRONOUS

from beehive.model import ContrastiveBeehiveModel, BeehiveFineTuner
from beehive.data import FeatureNormalizer
from beehive.config import MODEL_CFG, HIVE_STATES
from beehive.inference import BeehiveInference

# ── Logging ───────────────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("inference_worker")

# ── Bucket names ──────────────────────────────────────────────────────────────
BUCKET_SENSORS     = "sensors"
BUCKET_ANOMALY     = "anomaly"
BUCKET_PREDICTIONS = "predictions"
BUCKETS_TO_CREATE  = [BUCKET_ANOMALY, BUCKET_PREDICTIONS]


# ── Bucket auto-creation ──────────────────────────────────────────────────────

def ensure_buckets(client: InfluxDBClient, org: str) -> None:
    """Create anomaly and predictions buckets if they don't already exist."""
    buckets_api = client.buckets_api()
    existing = {b.name for b in buckets_api.find_buckets().buckets}

    for bucket_name in BUCKETS_TO_CREATE:
        if bucket_name not in existing:
            buckets_api.create_bucket(bucket_name=bucket_name, org=org)
            log.info(f"Bucket '{bucket_name}' créé.")
        else:
            log.info(f"Bucket '{bucket_name}' déjà présent.")


# ── Model loading ─────────────────────────────────────────────────────────────

def load_engine(
    checkpoint: Path,
    norm_in:    Path,
    norm_out:   Path,
) -> BeehiveInference:
    """Load the fine-tuned model and return a BeehiveInference engine."""
    backbone  = ContrastiveBeehiveModel(MODEL_CFG)
    finetuner = BeehiveFineTuner(backbone)

    state = torch.load(checkpoint, map_location="cpu", weights_only=True)
    finetuner.load_state_dict(state)
    finetuner.eval()

    n_in  = FeatureNormalizer.load(str(norm_in))
    n_out = FeatureNormalizer.load(str(norm_out))

    log.info(f"Modèle chargé depuis {checkpoint}")
    return BeehiveInference(finetuner, n_in, n_out)


# ── InfluxDB query ────────────────────────────────────────────────────────────

def query_recent_sensors(
    client:   InfluxDBClient,
    org:      str,
    hive_id:  str,
    lookback: int,
) -> list[dict]:
    """
    Query the sensors bucket for the last `lookback` seconds.
    Returns a list of feature dicts, one per timestamp where all 17+17
    features are present for both sensors.
    """
    flux = f'''
    from(bucket: "{BUCKET_SENSORS}")
      |> range(start: -{lookback}s)
      |> filter(fn: (r) => r["beehive_id"] == "{hive_id}")
      |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
      |> sort(columns: ["_time"])
    '''

    try:
        tables = client.query_api().query(flux, org=org)
    except Exception as e:
        log.warning(f"Erreur query InfluxDB : {e}")
        return []

    rows = []
    for table in tables:
        for record in table.records:
            values = record.values
            # Check all required fields are present
            required = (
                ["temperature_int", "humidity_int", "sound_freq_int", "sound_amp_int",
                 "temperature_ext", "humidity_ext", "sound_freq_ext", "sound_amp_ext"]
                + [f"mfcc_int_{i}" for i in range(13)]
                + [f"mfcc_ext_{i}" for i in range(13)]
            )
            if not all(k in values for k in required):
                continue

            rows.append({
                "ts":             record.get_time(),
                "ts_iso":         record.get_time().isoformat(),
                "t_in_C":         float(values["temperature_int"]),
                "h_in_pct":       float(values["humidity_int"]),
                "dom_freq_in_hz": float(values["sound_freq_int"]),
                "rms_in":         float(values["sound_amp_int"]),
                "t_out_C":        float(values["temperature_ext"]),
                "h_out_pct":      float(values["humidity_ext"]),
                "dom_freq_out_hz":float(values["sound_freq_ext"]),
                "rms_out":        float(values["sound_amp_ext"]),
                **{f"mfcc_in_{i}":  float(values[f"mfcc_int_{i}"]) for i in range(13)},
                **{f"mfcc_out_{i}": float(values[f"mfcc_ext_{i}"]) for i in range(13)},
                "timestamp_min":  0,
                "anomaly_flag":   0,
            })

    return rows


# ── InfluxDB write ────────────────────────────────────────────────────────────

def write_result(
    write_api,
    org:      str,
    result,
    hive_id:  str,
    ts_iso:   str,
) -> None:
    """Write inference result to the anomaly bucket."""
    point = (
        Point("anomaly_score")
        .tag("hive_id", hive_id)
        .field("p_normal",  float(result.probabilities[0]))
        .field("p_anomaly", float(result.probabilities[1]))
        .field("label",     result.label)
        .field("alert",     result.alert)
        .time(ts_iso, "s")
    )
    write_api.write(bucket=BUCKET_ANOMALY, org=org, record=point)


# ── Main loop ─────────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(description="Beetter inference worker")
    p.add_argument("--interval",   type=int,   default=30,
                   help="Polling interval in seconds (default: 30)")
    p.add_argument("--hive-id",    type=str,   default="B001",
                   help="Hive ID to filter from sensors bucket (default: B001)")
    p.add_argument("--env",        type=str,
                   default=str(_IA_DIR.parent / "app" / ".env"),
                   help="Path to .env file with InfluxDB credentials")
    p.add_argument("--checkpoint", type=str,
                   default=str(_IA_DIR / "checkpoints" / "finetune_best.pt"))
    p.add_argument("--norm-in",    type=str,
                   default=str(_IA_DIR / "calibration" / "norm_in.json"))
    p.add_argument("--norm-out",   type=str,
                   default=str(_IA_DIR / "calibration" / "norm_out.json"))
    p.add_argument("--lookback",   type=int,   default=120,
                   help="Seconds back to query on each poll (default: 120)")
    args = p.parse_args()

    # ── Load .env ─────────────────────────────────────────────────────────────
    env_path = Path(args.env)
    if not env_path.exists():
        log.warning(f".env non trouvé : {env_path} — utilisation des variables d'environnement système")
    else:
        load_dotenv(env_path)
        log.info(f".env chargé depuis {env_path}")

    import os
    url   = os.getenv("INFLUXDB_URL",   "http://localhost:8086")
    token = os.getenv("INFLUXDB_TOKEN", "")
    org   = os.getenv("INFLUXDB_ORG",  "")

    if not token:
        log.error("INFLUXDB_TOKEN manquant dans .env — arrêt.")
        sys.exit(1)

    # ── Validate paths ────────────────────────────────────────────────────────
    for path, name in [
        (args.checkpoint, "--checkpoint"),
        (args.norm_in,    "--norm-in"),
        (args.norm_out,   "--norm-out"),
    ]:
        if not Path(path).exists():
            log.error(f"Fichier introuvable ({name}): {path}")
            sys.exit(1)

    # ── Connect to InfluxDB ───────────────────────────────────────────────────
    log.info(f"Connexion InfluxDB : {url}")
    client    = InfluxDBClient(url=url, token=token, org=org)
    write_api = client.write_api(write_options=SYNCHRONOUS)

    # ── Auto-create buckets ───────────────────────────────────────────────────
    ensure_buckets(client, org)

    # ── Load inference engine ─────────────────────────────────────────────────
    engine = load_engine(
        Path(args.checkpoint),
        Path(args.norm_in),
        Path(args.norm_out),
    )

    log.info(f"Worker démarré — ruche={args.hive_id}  "
             f"intervalle={args.interval}s  lookback={args.lookback}s")
    log.info("Ctrl+C pour arrêter.\n")

    # ── Polling loop ──────────────────────────────────────────────────────────
    seen_timestamps = set()   # avoid processing the same row twice

    try:
        while True:
            rows = query_recent_sensors(client, org, args.hive_id, args.lookback)
            new_rows = [r for r in rows if r["ts"] not in seen_timestamps]

            if not new_rows:
                log.debug("Aucune nouvelle donnée.")
            else:
                for row in new_rows:
                    try:
                        result = engine.infer_from_features(row)
                        write_result(write_api, org, result, args.hive_id, row["ts_iso"])
                        seen_timestamps.add(row["ts"])

                        alert_str = "  ⚠  ALERTE" if result.alert else ""
                        log.info(
                            f"[{row['ts_iso']}]  {result.label:<8s}  "
                            f"normal={result.probabilities[0]:.0%}  "
                            f"anomaly={result.probabilities[1]:.0%}"
                            f"{alert_str}"
                        )
                    except Exception as e:
                        log.warning(f"Erreur inférence sur {row['ts_iso']} : {e}")

            # Cap seen_timestamps memory to last 1000 entries
            if len(seen_timestamps) > 1000:
                seen_timestamps = set(list(seen_timestamps)[-500:])

            time.sleep(args.interval)

    except KeyboardInterrupt:
        log.info("Arrêt demandé.")
    finally:
        client.close()
        log.info("Connexion InfluxDB fermée.")


if __name__ == "__main__":
    main()
