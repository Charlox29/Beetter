"""
IA/label_by_cluster.py
─────────────────────────────────────────────────────────────────────────────
Apply binary labels (0=normal, 1=anomaly) to your CSV based on the cluster
IDs identified in cluster_timestamps.py.

Run cluster_timestamps.py first, read cluster_report.txt, then call this.

Usage:
  python label_by_cluster.py \\
      --clustered  cluster_output/data_clustered.csv \\
      --anomaly-clusters 0,3 \\
      --out data/hive1_labeled.csv

Then fine-tune:
  python fit_and_train.py --mode finetune --csv data/hive1_labeled.csv
"""

import argparse
import sys
from pathlib import Path

import pandas as pd


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--clustered", default="cluster_output/data_clustered.csv",
                   help="Tagged CSV produced by cluster_timestamps.py.")
    p.add_argument("--anomaly-clusters", required=True,
                   help="Comma-separated cluster IDs to label as anomaly (1). "
                        "Example: --anomaly-clusters 0,3")
    p.add_argument("--out", required=True,
                   help="Output CSV path (ready for fit_and_train.py).")
    args = p.parse_args()

    if not Path(args.clustered).exists():
        sys.exit(f"ERROR: --clustered file not found: {args.clustered}\n"
                 "Run cluster_timestamps.py first.")

    anomaly_ids = set()
    for x in args.anomaly_clusters.split(","):
        x = x.strip()
        if not x.isdigit():
            sys.exit(f"ERROR: '{x}' is not a valid cluster ID. Use integers only.")
        anomaly_ids.add(int(x))

    df = pd.read_csv(args.clustered)

    if "cluster" not in df.columns:
        sys.exit("ERROR: 'cluster' column not found. Re-run cluster_timestamps.py.")

    all_clusters = set(df["cluster"].unique())
    unknown = anomaly_ids - all_clusters
    if unknown:
        print(f"WARNING: cluster IDs {unknown} don't exist in the data "
              f"(available: {sorted(all_clusters)}). They will be ignored.")

    df["label"] = df["cluster"].apply(lambda c: 1 if c in anomaly_ids else 0)

    # Drop the cluster column — fit_and_train.py doesn't expect it
    df = df.drop(columns=["cluster"])

    n_anomaly = (df["label"] == 1).sum()
    n_normal  = (df["label"] == 0).sum()

    print(f"\nLabel distribution:")
    print(f"  normal  (0): {n_normal:>5}  ({n_normal/len(df)*100:.1f}%)")
    print(f"  anomaly (1): {n_anomaly:>5}  ({n_anomaly/len(df)*100:.1f}%)")

    if n_anomaly < 15:
        print(f"\nWARNING: only {n_anomaly} anomaly rows — fine-tuning needs at least 15.")
        print("Consider adding more anomaly clusters or reducing --n-clusters "
              "in cluster_timestamps.py to merge small clusters.")

    ratio = max(n_normal, n_anomaly) / max(min(n_normal, n_anomaly), 1)
    if ratio > 10:
        print(f"\nWARNING: class imbalance ratio is {ratio:.0f}x.")
        print("The BalancedClassSampler in fit_and_train.py handles this automatically,")
        print("but very high imbalance (>20x) may still hurt recall on the minority class.")

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(out_path, index=False)
    print(f"\n✓  Labeled CSV saved → {out_path}  ({len(df):,} rows)")
    print(f"\nNext step — fine-tune:")
    print(f"  python fit_and_train.py --mode finetune --csv {out_path}")


if __name__ == "__main__":
    main()
