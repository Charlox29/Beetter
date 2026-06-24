"""
IA/explore_embeddings.py
─────────────────────────────────────────────────────────────────────────────
Color-coded t-SNE of the pre-trained backbone embeddings.

Instead of coloring by class label (which we don't have), this script colors
each point by a raw sensor feature value — one PNG per feature. This lets you
visually identify what each cluster physically represents.

How to read the output:
  - If a cluster is uniformly red/blue on the temperature plot → temperature
    drove that separation.
  - If clusters look mixed on a feature plot → that feature didn't contribute
    much to the separation.
  - Compare all plots side by side to build a picture of each cluster.

Usage (from Beetter/IA/):
  python explore_embeddings.py --csv data/hive1_clean.csv

Optional flags:
  --checkpoint  checkpoints/pretrain_best.pt   (default)
  --norm-in     calibration/norm_in.json       (default)
  --norm-out    calibration/norm_out.json      (default)
  --out-dir     tsne_explore/                  (default)
  --max-samples 2000                           (default, keeps t-SNE fast)
  --perplexity  30                             (default)
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import torch
from torch.utils.data import DataLoader

# ── Make sure the beehive package is importable from IA/ ─────────────────────
sys.path.insert(0, str(Path(__file__).parent))
from beehive.model import ContrastiveBeehiveModel
from beehive.data  import BeehiveDataset, FeatureNormalizer, _build_raw_arrays
from beehive.config import MODEL_CFG


# ── Features to plot ──────────────────────────────────────────────────────────
# Each entry: (column_in_csv, display_label, colormap)
# 'coolwarm' = red/blue diverging (good for temperature, frequency)
# 'viridis'  = perceptually uniform (good for MFCC, RMS)
FEATURES_TO_PLOT = [
    # Interior sensor
    ("t_in_C",           "Interior temperature (°C)",     "coolwarm"),
    ("h_in_pct",         "Interior humidity (%)",          "Blues"),
    ("dom_freq_in_hz",   "Interior dominant freq (Hz)",    "coolwarm"),
    ("rms_in",           "Interior RMS amplitude",         "viridis"),
    ("mfcc_in_0",        "Interior MFCC-0 (energy)",       "viridis"),
    ("mfcc_in_1",        "Interior MFCC-1",                "viridis"),
    ("mfcc_in_2",        "Interior MFCC-2",                "viridis"),
    # Exterior sensor
    ("t_out_C",          "Exterior temperature (°C)",      "coolwarm"),
    ("h_out_pct",        "Exterior humidity (%)",           "Blues"),
    ("dom_freq_out_hz",  "Exterior dominant freq (Hz)",     "coolwarm"),
    ("rms_out",          "Exterior RMS amplitude",          "viridis"),
    ("mfcc_out_0",       "Exterior MFCC-0 (energy)",        "viridis"),
    # Difference signals (often most diagnostic)
    ("_delta_temp",      "ΔTemperature (in − out)",         "coolwarm"),
    ("_delta_freq",      "ΔDominant freq (in − out)",       "coolwarm"),
    ("_delta_rms",       "ΔRMS (in − out)",                 "coolwarm"),
]


def load_model(checkpoint: Path, device: str) -> ContrastiveBeehiveModel:
    model = ContrastiveBeehiveModel(MODEL_CFG)
    state = torch.load(checkpoint, map_location=device)
    # fit_and_train.py saves either the raw state_dict or a dict with a key
    if isinstance(state, dict) and "model_state_dict" in state:
        state = state["model_state_dict"]
    model.load_state_dict(state)
    model.to(device).eval()
    return model


def compute_embeddings(
    model:   ContrastiveBeehiveModel,
    dataset: BeehiveDataset,
    device:  str,
    max_samples: int,
) -> np.ndarray:
    """Return concatenated (h_in || h_out) embeddings, shape (N, 64)."""
    loader = DataLoader(dataset, batch_size=256, shuffle=False)
    parts = []
    n = 0
    with torch.no_grad():
        for x_in, x_out in loader:
            if n >= max_samples:
                break
            h_in, h_out = model.encode(x_in.to(device), x_out.to(device))
            combined = torch.cat([h_in, h_out], dim=-1)  # (B, 64)
            parts.append(combined.cpu().numpy())
            n += len(x_in)
    return np.concatenate(parts, axis=0)[:max_samples]


def run_tsne(embeddings: np.ndarray, perplexity: int) -> np.ndarray:
    from sklearn.manifold import TSNE
    print(f"  Running t-SNE on {len(embeddings)} points (perplexity={perplexity}) …")
    tsne = TSNE(
        n_components=2,
        perplexity=min(perplexity, len(embeddings) - 1),
        random_state=42,
        n_jobs=-1,
    )
    return tsne.fit_transform(embeddings)


def add_derived_columns(df: pd.DataFrame) -> pd.DataFrame:
    """Add difference signals that are often more diagnostic than raw values."""
    df = df.copy()
    df["_delta_temp"] = df["t_in_C"]        - df["t_out_C"]
    df["_delta_freq"] = df["dom_freq_in_hz"] - df["dom_freq_out_hz"]
    df["_delta_rms"]  = df["rms_in"]         - df["rms_out"]
    return df


def plot_all_features(
    tsne_xy:  np.ndarray,
    df:       pd.DataFrame,
    out_dir:  Path,
    max_samples: int,
) -> None:
    import matplotlib.pyplot as plt
    import matplotlib.cm as cm

    df = df.iloc[:max_samples].reset_index(drop=True)
    df = add_derived_columns(df)

    out_dir.mkdir(parents=True, exist_ok=True)

    n_plots = len(FEATURES_TO_PLOT)
    print(f"\nSaving {n_plots} feature plots to {out_dir}/ …")

    for col, label, cmap in FEATURES_TO_PLOT:
        if col not in df.columns:
            print(f"  Skipping '{col}' — column not found in CSV.")
            continue

        values = df[col].values.astype(float)

        # Clip extreme outliers for a cleaner colormap
        # (outliers would compress the colormap and make everything look the same)
        p2, p98 = np.percentile(values, [2, 98])
        values_clipped = np.clip(values, p2, p98)

        fig, ax = plt.subplots(figsize=(8, 7))
        ax.set_title(f"Embedding space — colored by:\n{label}", fontsize=12)
        ax.set_xlabel("t-SNE dim 1")
        ax.set_ylabel("t-SNE dim 2")
        ax.grid(True, alpha=0.2)

        sc = ax.scatter(
            tsne_xy[:, 0], tsne_xy[:, 1],
            c=values_clipped,
            cmap=cmap,
            s=12,
            alpha=0.7,
            linewidths=0,
        )
        cbar = plt.colorbar(sc, ax=ax, pad=0.01)
        cbar.set_label(label, fontsize=9)

        # Annotate min/max so you know the actual range (before clipping)
        ax.text(
            0.01, 0.01,
            f"range: [{values.min():.2f} … {values.max():.2f}]  (colormap clipped to p2–p98)",
            transform=ax.transAxes,
            fontsize=7,
            color="gray",
        )

        plt.tight_layout()

        safe_name = col.replace("_", "").replace(" ", "_")
        out_path = out_dir / f"tsne_{safe_name}.png"
        fig.savefig(out_path, dpi=150)
        plt.close(fig)
        print(f"  ✓  {out_path.name}")

    # ── Bonus: overview grid (all features in one figure) ────────────────────
    n_cols = 3
    n_rows = -(-n_plots // n_cols)  # ceil division
    fig, axes = plt.subplots(n_rows, n_cols, figsize=(6 * n_cols, 5 * n_rows))
    axes = axes.flatten()

    for i, (col, label, cmap) in enumerate(FEATURES_TO_PLOT):
        ax = axes[i]
        if col not in df.columns:
            ax.axis("off")
            continue
        values = df[col].values.astype(float)
        p2, p98 = np.percentile(values, [2, 98])
        values_clipped = np.clip(values, p2, p98)

        sc = ax.scatter(tsne_xy[:, 0], tsne_xy[:, 1],
                        c=values_clipped, cmap=cmap, s=5, alpha=0.6, linewidths=0)
        ax.set_title(label, fontsize=9)
        ax.set_xticks([]); ax.set_yticks([])
        plt.colorbar(sc, ax=ax, pad=0.01)

    # Hide unused subplots
    for j in range(i + 1, len(axes)):
        axes[j].axis("off")

    plt.suptitle("Embedding space — all features overview", fontsize=13, y=1.01)
    plt.tight_layout()
    overview_path = out_dir / "tsne_overview.png"
    fig.savefig(overview_path, dpi=130, bbox_inches="tight")
    plt.close(fig)
    print(f"\n  ✓  Overview grid → {overview_path.name}")


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(description="Color-coded t-SNE of pre-trained embeddings.")
    p.add_argument("--csv",         required=True,
                   help="Cleaned CSV (output of prepare_csv.py).")
    p.add_argument("--checkpoint",  default="checkpoints/pretrain_best.pt",
                   help="Pre-trained model checkpoint.")
    p.add_argument("--norm-in",     default="calibration/norm_in.json")
    p.add_argument("--norm-out",    default="calibration/norm_out.json")
    p.add_argument("--out-dir",     default="tsne_explore",
                   help="Output folder for PNG files.")
    p.add_argument("--max-samples", type=int, default=2000,
                   help="Cap samples fed to t-SNE (keeps it fast).")
    p.add_argument("--perplexity",  type=int, default=30)
    args = p.parse_args()

    # ── Validate paths ────────────────────────────────────────────────────────
    for path, name in [
        (args.csv,        "--csv"),
        (args.checkpoint, "--checkpoint"),
        (args.norm_in,    "--norm-in"),
        (args.norm_out,   "--norm-out"),
    ]:
        if not Path(path).exists():
            sys.exit(f"ERROR: {name} not found: {path}")

    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"\nDevice: {device}")

    # ── Load model ────────────────────────────────────────────────────────────
    print(f"Loading checkpoint: {args.checkpoint}")
    model = load_model(Path(args.checkpoint), device)

    # ── Load normalizers ──────────────────────────────────────────────────────
    norm_in  = FeatureNormalizer.load(args.norm_in)
    norm_out = FeatureNormalizer.load(args.norm_out)

    # ── Build dataset ─────────────────────────────────────────────────────────
    print(f"Loading CSV: {args.csv}")
    dataset = BeehiveDataset(args.csv, norm_in, norm_out)
    print(f"  {len(dataset)} samples available, using up to {args.max_samples}.")

    # ── Compute embeddings ────────────────────────────────────────────────────
    print("Computing backbone embeddings …")
    embeddings = compute_embeddings(model, dataset, device, args.max_samples)
    print(f"  Embeddings shape: {embeddings.shape}")

    # ── t-SNE ─────────────────────────────────────────────────────────────────
    tsne_xy = run_tsne(embeddings, args.perplexity)

    # ── Load raw CSV values for coloring ─────────────────────────────────────
    df = pd.read_csv(args.csv).iloc[:args.max_samples].reset_index(drop=True)

    # ── Plot ───────────────────────────────────────────────────────────────────
    plot_all_features(tsne_xy, df, Path(args.out_dir), args.max_samples)

    print(f"\nDone. Open the '{args.out_dir}/' folder and look at:")
    print("  1. tsne_overview.png       — all features at a glance")
    print("  2. Individual PNGs         — one per feature, full resolution")
    print("\nWhat to look for:")
    print("  Uniform color within a cluster → that feature drove the separation.")
    print("  Mixed colors within a cluster  → that feature didn't matter much.")
    print("  _delta_* plots are often the most diagnostic.\n")


if __name__ == "__main__":
    main()
