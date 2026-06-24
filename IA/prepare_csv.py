"""
IA/prepare_csv.py
─────────────────────────────────────────────────────────────────────────────
Clean an InfluxDB-exported CSV and produce a file ready for fit_and_train.py.

Three operations performed in order:
  1. Rename columns  InfluxDB names → collect_training_data.py names
  2. Drop 36 interior-MFCC spike rows
  3. Forward-fill exterior T/H zeros (binary sensor crash, not drift)

Usage (from Beetter/IA/):
  python prepare_csv.py --csv data/influx_export.csv --out data/hive1_clean.csv

Then pre-train:
  python fit_and_train.py --mode pretrain --csv data/hive1_clean.csv --tsne
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd


# ── Column rename maps (matches collect_training_data.py exactly) ─────────────

_INT_RENAME = {
    "temperature_int":  "t_in_C",
    "humidity_int":     "h_in_pct",
    "sound_amp_int":    "rms_in",
    "sound_freq_int":   "dom_freq_in_hz",
    **{f"mfcc_int_{i}": f"mfcc_in_{i}" for i in range(13)},
}
_EXT_RENAME = {
    "temperature_ext":  "t_out_C",
    "humidity_ext":     "h_out_pct",
    "sound_amp_ext":    "rms_out",
    "sound_freq_ext":   "dom_freq_out_hz",
    **{f"mfcc_ext_{i}": f"mfcc_out_{i}" for i in range(13)},
}
_ALL_RENAME = {**_INT_RENAME, **_EXT_RENAME}

# Columns _build_raw_arrays() in data.py requires
_REQUIRED_COLS = (
    ["t_in_C", "h_in_pct", "rms_in",  "dom_freq_in_hz"]
    + [f"mfcc_in_{i}"  for i in range(13)]
    + ["t_out_C", "h_out_pct", "rms_out", "dom_freq_out_hz"]
    + [f"mfcc_out_{i}" for i in range(13)]
)

MFCC_IN_COLS  = [f"mfcc_in_{i}"  for i in range(13)]
MFCC_OUT_COLS = [f"mfcc_out_{i}" for i in range(13)]


# ── Step 1: rename ────────────────────────────────────────────────────────────

def rename_columns(df: pd.DataFrame) -> pd.DataFrame:
    """Rename InfluxDB field names to collect_training_data.py names."""
    present = {k: v for k, v in _ALL_RENAME.items() if k in df.columns}
    df = df.rename(columns=present)
    print(f"  Renamed {len(present)} columns.")

    missing = [c for c in _REQUIRED_COLS if c not in df.columns]
    if missing:
        print(f"\nERROR: {len(missing)} required columns still missing after rename:")
        for c in missing[:10]:
            print(f"  '{c}'")
        if len(missing) > 10:
            print(f"  … and {len(missing) - 10} more")
        print("\nCheck that your CSV came from the InfluxDB export (not from")
        print("collect_training_data.py, which already has the correct names).")
        sys.exit(1)

    return df


# ── Step 2: drop interior-MFCC spike rows ────────────────────────────────────

def drop_interior_mfcc_spikes(df: pd.DataFrame, sigma_threshold: float = 5.0) -> pd.DataFrame:
    """
    Drop rows where any interior MFCC coefficient is a clear outlier.

    Strategy: flag a row if ANY of its 13 interior MFCC values sits more than
    `sigma_threshold` standard deviations from that coefficient's median.
    σ=5 is very conservative: for normally distributed data, fewer than
    3 false positives are expected across 11,000 rows with 13 coefficients.

    Args:
        df:               DataFrame after rename.
        sigma_threshold:  Number of σ beyond which a value is a spike (default 5).
    Returns:
        Cleaned DataFrame.
    """
    mfcc_data = df[MFCC_IN_COLS]

    # Robust stats: use median and IQR-derived σ to avoid circular reasoning
    # (i.e., if spikes inflate the mean/std, they'd also suppress the threshold).
    median = mfcc_data.median()
    iqr    = mfcc_data.quantile(0.75) - mfcc_data.quantile(0.25)
    robust_sigma = iqr / 1.3489  # IQR → σ for a Gaussian (1.3489 = 2*Φ⁻¹(0.75))
    robust_sigma = robust_sigma.clip(lower=1e-6)  # avoid division by zero

    # A row is a spike if ANY of its 13 MFCC-in values is far from the median
    z_scores = (mfcc_data - median).abs() / robust_sigma
    is_spike = (z_scores > sigma_threshold).any(axis=1)

    n_spikes = is_spike.sum()
    if n_spikes == 0:
        print("  No interior MFCC spikes detected.")
    else:
        print(f"  Dropping {n_spikes} spike rows ({n_spikes / len(df) * 100:.2f}% of data).")
        if n_spikes > 200:
            print(f"  WARNING: {n_spikes} spikes is unusually high.")
            print("  Consider inspecting them: df[is_spike].to_csv('spikes_review.csv')")

    return df[~is_spike].reset_index(drop=True)


# ── Step 3: forward-fill exterior T/H zeros ──────────────────────────────────

def forward_fill_ext_th(df: pd.DataFrame) -> pd.DataFrame:
    """
    Forward-fill rows where exterior temperature AND humidity are exactly 0.0.

    The crash is binary: the SHT40 sensor locks at 0.0/0.0 during extended
    bad weather (not a gradual drift). Forward-fill propagates the last good
    reading until the sensor recovers. Any leading zeros (before the first
    good reading) are then back-filled as a fallback.
    """
    t_col, h_col = "t_out_C", "h_out_pct"

    # Identify crashed rows: exactly 0.0 on BOTH channels simultaneously
    crashed = (df[t_col] == 0.0) & (df[h_col] == 0.0)
    n_crashed = crashed.sum()

    if n_crashed == 0:
        print("  No exterior T/H zeros detected (sensor was healthy).")
        return df

    pct = n_crashed / len(df) * 100
    print(f"  Forward-filling {n_crashed} exterior T/H zeros ({pct:.1f}% of rows).")

    # Replace zeros with NaN, forward-fill, then back-fill leading NaNs
    df = df.copy()
    df.loc[crashed, [t_col, h_col]] = np.nan
    df[[t_col, h_col]] = df[[t_col, h_col]].ffill().bfill()

    remaining_nan = df[[t_col, h_col]].isna().sum().sum()
    if remaining_nan > 0:
        print(f"  WARNING: {remaining_nan} NaN values remain after ffill/bfill.")
        print("  This can happen if the CSV has NO valid exterior T/H reading at all.")
        print("  Filling with interior sensor values as fallback.")
        df[t_col] = df[t_col].fillna(df["t_in_C"])
        df[h_col] = df[h_col].fillna(df["h_in_pct"])

    return df


# ── Validation stats ──────────────────────────────────────────────────────────

def print_stats(df: pd.DataFrame, label: str) -> None:
    """Print key statistics on the dataset."""
    print(f"\n{'─'*55}")
    print(f"  {label}: {len(df):,} rows")
    print(f"{'─'*55}")

    checks = {
        "Interior T°C":     ("t_in_C",         24, 41.6),
        "Interior H%":      ("h_in_pct",         30,   70),
        "Exterior T°C":     ("t_out_C",          -5,   45),
        "Exterior H%":      ("h_out_pct",          0,  100),
        "MFCC_in_0 (C0)":   ("mfcc_in_0",       -80,   20),
        "MFCC_out_0 (C0)":  ("mfcc_out_0",      -80,   20),
        "RMS_in":           ("rms_in",             0,    5),
        "RMS_out":          ("rms_out",            0,    5),
    }
    for label_, (col, lo, hi) in checks.items():
        if col not in df.columns:
            continue
        s = df[col]
        pct_zero = (s == 0.0).mean() * 100
        print(f"  {label_:<20s}  μ={s.mean():7.2f}  σ={s.std():6.2f}"
              f"  [{s.min():7.2f}…{s.max():7.2f}]"
              f"  {pct_zero:.0f}% zero")


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(
        description="Clean an InfluxDB CSV export for Beetter pre-training."
    )
    p.add_argument("--csv",    required=True,
                   help="Path to the raw InfluxDB export CSV.")
    p.add_argument("--out",    required=True,
                   help="Path for the cleaned output CSV.")
    p.add_argument("--sigma",  type=float, default=5.0,
                   help="Interior MFCC spike detection threshold in σ (default: 5.0).")
    p.add_argument("--no-spike-drop", action="store_true",
                   help="Skip the spike-dropping step (useful for debugging).")
    p.add_argument("--no-ffill",      action="store_true",
                   help="Skip the exterior T/H forward-fill step.")
    args = p.parse_args()

    csv_path = Path(args.csv)
    out_path = Path(args.out)

    if not csv_path.exists():
        sys.exit(f"ERROR: Input CSV not found: {csv_path}")

    print(f"\nLoading {csv_path} …")
    df = pd.read_csv(csv_path)
    print(f"  Raw rows: {len(df):,}  |  Columns: {len(df.columns)}")
    print_stats(df, "RAW")

    # ── Step 1: rename ────────────────────────────────────────────────────────
    print("\n[1/3] Renaming columns …")
    # Check if this CSV is already in the training format (already renamed)
    already_renamed = "t_in_C" in df.columns
    if already_renamed:
        print("  Columns appear to already be in training format — skipping rename.")
    else:
        df = rename_columns(df)

    # ── Step 2: drop spikes ───────────────────────────────────────────────────
    print("\n[2/3] Dropping interior MFCC spike rows …")
    if args.no_spike_drop:
        print("  Skipped (--no-spike-drop).")
    else:
        df = drop_interior_mfcc_spikes(df, sigma_threshold=args.sigma)

    # ── Step 3: forward-fill ext T/H ─────────────────────────────────────────
    print("\n[3/3] Forward-filling exterior T/H sensor crashes …")
    if args.no_ffill:
        print("  Skipped (--no-ffill).")
    else:
        df = forward_fill_ext_th(df)

    # ── Final check ───────────────────────────────────────────────────────────
    print_stats(df, "CLEAN")

    remaining_nan = df[_REQUIRED_COLS].isna().sum().sum()
    if remaining_nan > 0:
        print(f"\nWARNING: {remaining_nan} NaN values remain in required columns.")
        print("  Dropping rows with any NaN before saving.")
        df = df.dropna(subset=_REQUIRED_COLS)
        print(f"  Rows after NaN drop: {len(df):,}")

    if len(df) < 200:
        print(f"\nERROR: Only {len(df)} rows after cleaning — minimum 200 needed to train.")
        sys.exit(1)

    # ── Save ──────────────────────────────────────────────────────────────────
    out_path.parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(out_path, index=False)
    print(f"\n✓  Cleaned CSV saved → {out_path}  ({len(df):,} rows)")

    print()
    print("Next step — pre-train:")
    print(f"  python fit_and_train.py --mode pretrain --csv {out_path} --tsne")
    print()
    print("What to watch:")
    print("  loss        starts near ln(64)=4.16  (random baseline)")
    print("  retrieval   starts near 1/64=0.016  (random baseline)")
    print("  Good sign:  retrieval_acc > 0.50 by epoch 100")


if __name__ == "__main__":
    main()
