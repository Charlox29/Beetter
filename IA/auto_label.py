"""
IA/auto_label.py
─────────────────────────────────────────────────────────────────────────────
Run three pre-trained Random Forest detectors (saved via joblib by a coworker)
on a collected LoRa CSV and write candidate labels for fine-tuning.

Detector → microphone → suggested class:
  detector_frelon.pkl    exterior (mfcc_out_0..12) → 5 (attack)
  detector_essaimage.pkl exterior (mfcc_out_0..12) → 2 (swarming)
  detector_reine.pkl     interior (mfcc_in_0..12)  → 3 (queen_competition)

Priority when multiple detectors fire: hornet > swarm > queen > 0 (normal).

Usage (from Beetter/IA/):
  python auto_label.py --csv data/hive1.csv
  python auto_label.py --csv data/hive1.csv --models-dir /path/to/models
"""

import argparse
import sys
from pathlib import Path

import joblib
import pandas as pd

from beehive.config import HIVE_STATES

MFCC_IN  = [f'mfcc_in_{i}'  for i in range(13)]
MFCC_OUT = [f'mfcc_out_{i}' for i in range(13)]

_DETECTOR_FILES = {
    'frelon':    'detector_frelon.pkl',
    'essaimage': 'detector_essaimage.pkl',
    'reine':     'detector_reine.pkl',
}


def _load_models(models_dir: Path) -> dict:
    models = {}
    for name, filename in _DETECTOR_FILES.items():
        path = models_dir / filename
        if not path.exists():
            sys.exit(
                f"ERROR: {path} not found.\n"
                f"Ask your coworker to export the '{name}' detector first:\n"
                f"  import joblib; joblib.dump(clf_{name}, '{path}')"
            )
        models[name] = joblib.load(path)
        print(f"  Loaded {filename}")
    return models


def _suggest(hornet: int, swarm: int, queen: int) -> int:
    if hornet:  return 5  # attack
    if swarm:   return 2  # swarming
    if queen:   return 3  # queen_competition
    return 0              # normal


def main():
    p = argparse.ArgumentParser(
        description="Auto-label a LoRa CSV using pre-trained RF detectors"
    )
    p.add_argument('--csv',        required=True,
                   help="Input CSV produced by collect_training_data.py")
    p.add_argument('--models-dir', default='models',
                   help="Directory containing the .pkl detector files (default: models)")
    args = p.parse_args()

    models_dir = Path(args.models_dir)
    csv_path   = Path(args.csv)

    print(f"Loading models from {models_dir}/")
    models = _load_models(models_dir)

    print(f"\nReading {csv_path} ...")
    df = pd.read_csv(csv_path)
    print(f"  {len(df)} rows")

    missing = [c for c in MFCC_IN + MFCC_OUT if c not in df.columns]
    if missing:
        sys.exit(
            f"ERROR: {len(missing)} expected MFCC columns are missing "
            f"(e.g. '{missing[0]}').\n"
            "Is this a collect_training_data.py output file?"
        )

    preds_hornet = models['frelon'].predict(df[MFCC_OUT])
    preds_swarm  = models['essaimage'].predict(df[MFCC_OUT])
    preds_queen  = models['reine'].predict(df[MFCC_IN])

    df['label_suggested'] = [
        _suggest(h, s, q)
        for h, s, q in zip(preds_hornet, preds_swarm, preds_queen)
    ]

    out_path = csv_path.with_stem(csv_path.stem + '_autolabeled')
    df.to_csv(out_path, index=False)
    print(f"\nSaved → {out_path}")

    print("\nSuggested label distribution:")
    counts = df['label_suggested'].value_counts().sort_index()
    for label_id, count in counts.items():
        state = HIVE_STATES[label_id] if label_id < len(HIVE_STATES) else '?'
        print(f"  {label_id} ({state:<20s}): {count:5d} rows")

    print("\n" + "─" * 62)
    print("IMPORTANT — human review required before fine-tuning:")
    print("  1. These are SUGGESTIONS only — verify each label before use.")
    print("  2. Only 3 of 6 classes are covered by the detectors:")
    print("       covered : 0=normal  2=swarming  3=queen_competition  5=attack")
    print("       missing : 1=pre_swarming and 4=queenless need manual labelling.")
    print("  3. After review, rename the column:")
    print("       'label_suggested' → 'label'")
    print("     then fine-tune:")
    print(f"       python fit_and_train.py --mode finetune --csv {out_path}")
    print("─" * 62)


if __name__ == '__main__':
    main()
