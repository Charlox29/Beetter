"""
IA/cluster_timestamps.py
─────────────────────────────────────────────────────────────────────────────
Map t-SNE clusters back to timestamps so you can identify what each cluster
represents and label it as normal (0) or anomaly (1).

What this script does:
  1. Loads the pre-trained backbone and computes embeddings (same as explore_embeddings.py)
  2. Runs t-SNE to get the 2D positions
  3. Clusters those 2D positions with KMeans
  4. For each cluster: prints the timestamp range, sensor statistics, and
     saves a CSV with every row tagged with its cluster ID
  5. Saves a color-coded t-SNE PNG so you can see which cluster is which number

After running this, you:
  - Open cluster_report.txt to read the stats per cluster
  - Decide which clusters are normal (0) and which are anomaly (1)
  - Run the labelling command printed at the end

Usage (from Beetter/IA/):
  python cluster_timestamps.py --csv data/hive1_clean.csv

Optional flags:
  --n-clusters   7          Number of KMeans clusters (default: 7, tune if needed)
  --checkpoint   checkpoints/pretrain_best.pt
  --norm-in      calibration/norm_in.json
  --norm-out     calibration/norm_out.json
  --out-dir      cluster_output/
  --max-samples  2000
  --perplexity   30
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import torch
from torch.utils.data import DataLoader

sys.path.insert(0, str(Path(__file__).parent))
from beehive.model import ContrastiveBeehiveModel
from beehive.data  import BeehiveDataset, FeatureNormalizer
from beehive.config import MODEL_CFG


# ── Helpers ───────────────────────────────────────────────────────────────────

def load_model(checkpoint: Path, device: str) -> ContrastiveBeehiveModel:
    model = ContrastiveBeehiveModel(MODEL_CFG)
    state = torch.load(checkpoint, map_location=device)
    if isinstance(state, dict) and "model_state_dict" in state:
        state = state["model_state_dict"]
    model.load_state_dict(state)
    return model.to(device).eval()


def compute_embeddings(model, dataset, device, max_samples):
    loader = DataLoader(dataset, batch_size=256, shuffle=False)
    parts, n = [], 0
    with torch.no_grad():
        for x_in, x_out in loader:
            if n >= max_samples:
                break
            h_in, h_out = model.encode(x_in.to(device), x_out.to(device))
            parts.append(torch.cat([h_in, h_out], dim=-1).cpu().numpy())
            n += len(x_in)
    return np.concatenate(parts)[:max_samples]


def run_tsne(embeddings, perplexity):
    from sklearn.manifold import TSNE
    print(f"  Running t-SNE on {len(embeddings)} points …")
    return TSNE(
        n_components=2,
        perplexity=min(perplexity, len(embeddings) - 1),
        random_state=42,
        n_jobs=-1,
    ).fit_transform(embeddings)


def run_kmeans(tsne_xy, n_clusters):
    from sklearn.cluster import KMeans
    print(f"  Running KMeans with {n_clusters} clusters …")
    km = KMeans(n_clusters=n_clusters, random_state=42, n_init=20)
    return km.fit_predict(tsne_xy)


def plot_clusters(tsne_xy, cluster_ids, out_path):
    import matplotlib.pyplot as plt
    import matplotlib.cm as cm

    n_clusters = len(np.unique(cluster_ids))
    colors = cm.tab10(np.linspace(0, 1, n_clusters))

    fig, ax = plt.subplots(figsize=(9, 7))
    ax.set_title("Embedding space — KMeans clusters\n(numbers match cluster_report.txt)", fontsize=12)
    ax.set_xlabel("t-SNE dim 1")
    ax.set_ylabel("t-SNE dim 2")
    ax.grid(True, alpha=0.2)

    for cid in range(n_clusters):
        mask = cluster_ids == cid
        ax.scatter(
            tsne_xy[mask, 0], tsne_xy[mask, 1],
            c=[colors[cid]], label=f"Cluster {cid}  (n={mask.sum()})",
            s=14, alpha=0.75, linewidths=0,
        )
        # Label the cluster centroid with its number
        cx, cy = tsne_xy[mask, 0].mean(), tsne_xy[mask, 1].mean()
        ax.text(cx, cy, str(cid), fontsize=11, fontweight="bold",
                ha="center", va="center",
                bbox=dict(boxstyle="round,pad=0.2", fc="white", alpha=0.7))

    ax.legend(loc="upper right", fontsize=8, framealpha=0.8)
    plt.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"  ✓  Cluster map → {out_path.name}")


def print_cluster_report(df_slice, cluster_ids, out_path):
    """
    For each cluster: timestamp range + key sensor stats.
    Writes both to stdout and to a text file.
    """
    STAT_COLS = [
        "t_in_C", "h_in_pct", "dom_freq_in_hz", "rms_in",
        "t_out_C", "h_out_pct", "dom_freq_out_hz", "rms_out",
        "mfcc_in_0",
    ]
    # Add delta columns
    df_slice = df_slice.copy()
    df_slice["_delta_temp"] = df_slice["t_in_C"]         - df_slice["t_out_C"]
    df_slice["_delta_freq"] = df_slice["dom_freq_in_hz"]  - df_slice["dom_freq_out_hz"]
    STAT_COLS += ["_delta_temp", "_delta_freq"]

    df_slice = df_slice.reset_index(drop=True)
    df_slice["cluster"] = cluster_ids

    lines = []
    lines.append("=" * 70)
    lines.append("  CLUSTER REPORT — use this to assign normal(0) / anomaly(1) labels")
    lines.append("=" * 70)

    has_timestamp = "timestamp" in df_slice.columns

    for cid in sorted(df_slice["cluster"].unique()):
        sub = df_slice[df_slice["cluster"] == cid]
        lines.append(f"\n{'─'*70}")
        lines.append(f"  CLUSTER {cid}   ({len(sub)} rows, {len(sub)/len(df_slice)*100:.1f}% of data)")
        lines.append(f"{'─'*70}")

        if has_timestamp:
            try:
                ts = pd.to_datetime(sub["timestamp"])
                lines.append(f"  Time range : {ts.min()}  →  {ts.max()}")
                lines.append(f"  Duration   : {ts.max() - ts.min()}")
                # Show the 5 most recent timestamps (most memorable for the beekeeper)
                lines.append(f"  Last 5 timestamps:")
                for t in ts.sort_values().tail(5):
                    lines.append(f"    {t}")
            except Exception:
                lines.append(f"  Timestamps : (could not parse)")

        lines.append("")
        lines.append(f"  {'Feature':<25s}  {'mean':>8s}  {'std':>7s}  {'min':>8s}  {'max':>8s}")
        lines.append(f"  {'─'*25}  {'─'*8}  {'─'*7}  {'─'*8}  {'─'*8}")
        for col in STAT_COLS:
            if col not in sub.columns:
                continue
            s = sub[col]
            lines.append(
                f"  {col:<25s}  {s.mean():>8.2f}  {s.std():>7.2f}"
                f"  {s.min():>8.2f}  {s.max():>8.2f}"
            )

    lines.append(f"\n{'='*70}")
    lines.append("  HOW TO LABEL")
    lines.append(f"{'='*70}")
    lines.append("  Look at the stats above and decide:")
    lines.append("    - High interior temp (>36°C) + distinct freq  → likely anomaly (1)")
    lines.append("    - Very high MFCC-0 (near 0 or positive)       → high acoustic energy → anomaly (1)")
    lines.append("    - Low temp (< 30°C), quiet RMS, normal freq   → night / rest → normal (0)")
    lines.append("    - Stable 33–36°C, moderate freq               → normal daytime (0)")
    lines.append("")
    lines.append("  Then run:")
    lines.append("    python label_by_cluster.py --csv data/hive1_clean.csv \\")
    lines.append("        --anomaly-clusters 2,5   # replace with your anomaly cluster IDs")
    lines.append("")

    report = "\n".join(lines)
    print(report)
    out_path.write_text(report, encoding="utf-8")
    print(f"\n  ✓  Report saved → {out_path.name}")

    return df_slice  # with cluster column added


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(description="Map embedding clusters to timestamps.")
    p.add_argument("--csv",          required=True)
    p.add_argument("--checkpoint",   default="checkpoints/pretrain_best.pt")
    p.add_argument("--norm-in",      default="calibration/norm_in.json")
    p.add_argument("--norm-out",     default="calibration/norm_out.json")
    p.add_argument("--out-dir",      default="cluster_output")
    p.add_argument("--n-clusters",   type=int, default=7,
                   help="Number of KMeans clusters. Try 6–9 and pick the one "
                        "that best matches the visual blobs in the t-SNE.")
    p.add_argument("--max-samples",  type=int, default=2000)
    p.add_argument("--perplexity",   type=int, default=30)
    args = p.parse_args()

    for path, name in [
        (args.csv,        "--csv"),
        (args.checkpoint, "--checkpoint"),
        (args.norm_in,    "--norm-in"),
        (args.norm_out,   "--norm-out"),
    ]:
        if not Path(path).exists():
            sys.exit(f"ERROR: {name} not found: {path}")

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"\nDevice: {device}")

    print("Loading model …")
    model = load_model(Path(args.checkpoint), device)

    norm_in  = FeatureNormalizer.load(args.norm_in)
    norm_out = FeatureNormalizer.load(args.norm_out)

    print(f"Loading CSV: {args.csv}")
    dataset = BeehiveDataset(args.csv, norm_in, norm_out)
    n = min(len(dataset), args.max_samples)
    print(f"  Using {n} / {len(dataset)} samples.")

    print("Computing embeddings …")
    embeddings = compute_embeddings(model, dataset, device, n)

    tsne_xy     = run_tsne(embeddings, args.perplexity)
    cluster_ids = run_kmeans(tsne_xy, args.n_clusters)

    # Load the raw CSV (same row order as the dataset)
    df = pd.read_csv(args.csv).iloc[:n].reset_index(drop=True)

    # ── Cluster map PNG ───────────────────────────────────────────────────────
    plot_clusters(tsne_xy, cluster_ids, out_dir / "clusters.png")

    # ── Text report ───────────────────────────────────────────────────────────
    df_clustered = print_cluster_report(df, cluster_ids, out_dir / "cluster_report.txt")

    # ── Tagged CSV (every row has its cluster ID) ─────────────────────────────
    tagged_path = out_dir / "data_clustered.csv"
    df_clustered.to_csv(tagged_path, index=False)
    print(f"  ✓  Tagged CSV  → {tagged_path.name}")

    print(f"\nAll output in: {out_dir}/")
    print("  clusters.png        — color-coded cluster map (numbers match the report)")
    print("  cluster_report.txt  — timestamp ranges + sensor stats per cluster")
    print("  data_clustered.csv  — your CSV with a 'cluster' column added")
    print()
    print("Next: read cluster_report.txt, decide which clusters are anomalies, then:")
    print("  python label_by_cluster.py --csv data/hive1_clean.csv --anomaly-clusters 0,3")


if __name__ == "__main__":
    main()
