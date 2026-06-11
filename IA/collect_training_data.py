"""
IA/collect_training_data.py
─────────────────────────────────────────────────────────────────────────────
Query InfluxDB and export a CSV file ready for beehive/data.py.

The exported CSV contains one row per timestamp where ALL 9 features are
present for BOTH sensors.  Rows with missing MFCC values are kept if you
pass --allow-partial — they are written with 0.0 for the missing coefficients
so you can pre-train on early data while MFCC firmware isn't deployed yet.

Column order (matches what BeehiveDataset._parse_dataframe() expects):
  timestamp,
  t_in_C, h_in_pct, rms_in, dom_freq_in_hz, mfcc_in_1..5,
  t_ext_C, h_out_pct, rms_out, dom_freq_out_hz, mfcc_out_1..5,
  anomaly_flag   (always 0 — computed on-device by ESP32, not stored in InfluxDB)

After export:
  - Unlabelled CSV → pre-training (Phase 1)
  - Add a 'label' column (int 0-5) for labelled events → fine-tuning (Phase 2)
    Label meanings (from beehive/config.py HIVE_STATES):
      0 = normal, 1 = pre_swarming, 2 = swarming,
      3 = queen_competition, 4 = queenless, 5 = attack

Usage (from Beetter/IA/):
  python collect_training_data.py --hive 1 --range 30d --out data/hive1.csv
  python collect_training_data.py --hive 1 --range 7d  --out data/hive1_partial.csv --allow-partial
"""

import argparse
import csv
import math
import os
import sys
from pathlib import Path

# ── Bootstrap: load Beetter app env so InfluxDB credentials are available ─────
_APP_DIR = Path(__file__).parents[1] / 'app'
sys.path.insert(0, str(_APP_DIR.parent))

from dotenv import load_dotenv
load_dotenv(_APP_DIR / '.env')

INFLUX_URL    = os.environ['INFLUXDB_URL']
INFLUX_TOKEN  = os.environ['INFLUXDB_TOKEN']
INFLUX_ORG    = os.environ['INFLUXDB_ORG']
INFLUX_BUCKET = os.environ['INFLUXDB_BUCKET']

# ── Column spec — exactly what BeehiveDataset._parse_dataframe() reads ────────
COLUMNS = [
    'timestamp',
    # inside sensor
    't_in_C', 'h_in_pct', 'rms_in', 'dom_freq_in_hz',
    'mfcc_in_1', 'mfcc_in_2', 'mfcc_in_3', 'mfcc_in_4', 'mfcc_in_5',
    # outside sensor
    't_out_C', 'h_out_pct', 'rms_out', 'dom_freq_out_hz',
    'mfcc_out_1', 'mfcc_out_2', 'mfcc_out_3', 'mfcc_out_4', 'mfcc_out_5',
    # anomaly flag (from ESP32 bitmask — not yet stored in InfluxDB, always 0)
    'anomaly_flag',
]

# InfluxDB measurement names → CSV column names
_INT_MAP = {
    'temperature_int': 't_in_C',
    'humidity_int':    'h_in_pct',
    'sound_amp_int':   'rms_in',        # amplitude ≈ RMS proxy
    'sound_freq_int':  'dom_freq_in_hz',
    'mfcc_int_1':      'mfcc_in_1',
    'mfcc_int_2':      'mfcc_in_2',
    'mfcc_int_3':      'mfcc_in_3',
    'mfcc_int_4':      'mfcc_in_4',
    'mfcc_int_5':      'mfcc_in_5',
}
_EXT_MAP = {
    'temperature_ext': 't_out_C',
    'humidity_ext':    'h_out_pct',
    'sound_amp_ext':   'rms_out',
    'sound_freq_ext':  'dom_freq_out_hz',
    'mfcc_ext_1':      'mfcc_out_1',
    'mfcc_ext_2':      'mfcc_out_2',
    'mfcc_ext_3':      'mfcc_out_3',
    'mfcc_ext_4':      'mfcc_out_4',
    'mfcc_ext_5':      'mfcc_out_5',
}
_ALL_INFLUX = list(_INT_MAP) + list(_EXT_MAP)
_MFCC_CSV_COLS = [c for c in COLUMNS if 'mfcc' in c]
_CORE_REQUIRED = [
    't_in_C', 'h_in_pct', 'rms_in', 'dom_freq_in_hz',
    't_out_C', 'h_out_pct', 'rms_out', 'dom_freq_out_hz',
]


def fetch(beehive_id: int, range_str: str) -> list[dict]:
    """Query InfluxDB, pivot all measurements onto a single row per timestamp."""
    from influxdb_client import InfluxDBClient

    meas_filter = ' or '.join(f'r._measurement == "{m}"' for m in _ALL_INFLUX)
    query = f"""
from(bucket: "{INFLUX_BUCKET}")
  |> range(start: -{range_str})
  |> filter(fn: (r) => r["beehive_id"] == "{beehive_id}")
  |> filter(fn: (r) => {meas_filter})
  |> pivot(rowKey: ["_time"], columnKey: ["_measurement"], valueColumn: "_value")
  |> sort(columns: ["_time"])
"""
    rows = []
    with InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG) as c:
        for table in c.query_api().query(query, org=INFLUX_ORG):
            for r in table.records:
                influx_row = dict(r.values)
                csv_row = {'timestamp': r.get_time().strftime('%Y-%m-%dT%H:%M:%SZ')}

                # Map InfluxDB measurement names → CSV column names
                for influx_key, csv_key in {**_INT_MAP, **_EXT_MAP}.items():
                    v = influx_row.get(influx_key)
                    csv_row[csv_key] = round(float(v), 4) if v is not None else None

                rows.append(csv_row)
    return rows


def filter_and_fill(rows: list[dict], allow_partial: bool) -> list[dict]:
    """
    Drop rows missing core environmental/acoustic features.
    If allow_partial=True, fill missing MFCC columns with 0.0 so you can
    pre-train on early data while the MFCC firmware isn't deployed yet.
    (Note: rows with 0 MFCC will weaken MFCC learning but won't break training.)
    """
    out = []
    n_dropped = 0
    for row in rows:
        # Always drop rows with missing core fields
        if any(row.get(c) is None for c in _CORE_REQUIRED):
            n_dropped += 1
            continue

        # Fill MFCC with 0 if allow_partial and not yet available
        has_mfcc = all(row.get(c) is not None for c in _MFCC_CSV_COLS)
        if not has_mfcc:
            if allow_partial:
                for c in _MFCC_CSV_COLS:
                    if row.get(c) is None:
                        row[c] = 0.0
            else:
                n_dropped += 1
                continue

        row['anomaly_flag'] = 0
        out.append(row)

    if n_dropped:
        print(f"  Dropped {n_dropped} rows with missing required fields")
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--hive',          type=int, required=True)
    p.add_argument('--range',         default='30d',
                   help="InfluxDB range string: 7d, 30d, 90d, ...")
    p.add_argument('--out',           required=True,
                   help="Output CSV path, e.g. data/hive1.csv")
    p.add_argument('--allow-partial', action='store_true',
                   help="Keep rows where MFCC is missing; fill with 0.0")
    args = p.parse_args()

    print(f"Querying hive={args.hive}  range={args.range}  ...")
    rows = fetch(args.hive, args.range)
    print(f"  Raw rows from InfluxDB: {len(rows)}")

    rows = filter_and_fill(rows, args.allow_partial)
    print(f"  Rows after filtering:   {len(rows)}")

    if not rows:
        print("No data to write. Is the simulator running? Try:")
        print("  python tools/simulate.py --burst 500")
        return

    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)
    print(f"Saved → {args.out}")

    mfcc_present = sum(1 for r in rows if r.get('mfcc_in_1', 0) != 0)
    print(f"  Rows with real MFCC: {mfcc_present}/{len(rows)}")

    if len(rows) < 500:
        print("⚠️  Fewer than 500 rows — collect more data before pre-training.")
    elif len(rows) < 2000:
        print("✓  Enough for a first pre-training run (2000+ rows ideal).")
    else:
        print("✓  Good dataset size — ready to pre-train.")

    print()
    print("Next steps:")
    print("  1. Pre-train:  python fit_and_train.py --mode pretrain --csv", args.out)
    print("  2. Label events: add a 'label' column (0-5) to the CSV")
    print("  3. Fine-tune:  python fit_and_train.py --mode finetune --csv <labeled_csv>")


if __name__ == '__main__':
    main()
