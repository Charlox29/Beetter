"""
tools/simulate.py
─────────────────────────────────────────────────────────────────────────────
Beetter — sensor data simulator.

Sends fake readings to the local Flask API, mimicking what the LoRa receiver
would do in production.  Now includes synthetic MFCC coefficients so the full
9-feature ML pipeline can be exercised before the real hardware is deployed.

MFCC simulation strategy
─────────────────────────
The five coefficients evolve with a sinusoidal daily cycle + noise.
Their absolute values are calibrated against published beehive MFCC ranges:
  C1 (energy):       -30 to -15 dB   (higher = louder/busier)
  C2 (spectral tilt): -10 to +10
  C3–C5:               -5 to  +5

Usage:
  python simulate.py                         one beehive (id=1), every 10s
  python simulate.py --ids 1 2 3             three beehives
  python simulate.py --interval 300          every 5 minutes (realistic)
  python simulate.py --burst 2000            inject 2000 past points (fill charts)
  python simulate.py --no-mfcc              omit MFCC (test pre-MFCC-firmware path)
"""

import argparse
import math
import random
import time
from datetime import datetime, timezone, timedelta

import requests

DEFAULT_URL      = "http://localhost:5000"
DEFAULT_IDS      = [1]
DEFAULT_INTERVAL = 10

TEMP_BASE = 35.0;  TEMP_AMP = 2.0
HUM_BASE  = 60.0;  HUM_AMP  = 8.0
SOUND_FREQ_BASE = 230.0;  SOUND_FREQ_AMP = 40.0
SOUND_AMP_BASE  = 0.35;   SOUND_AMP_AMP  = 0.15
LIGHT_DAY_MAX   = 3.0


def _mfcc(phase, base_c1, noise_scale=1.0):
    """
    Generate a plausible 5-MFCC vector for one sensor channel.

    base_c1 is the nominal C1 for this sensor/state (inside is typically
    louder → higher C1 than outside).  The daily cycle adds a small modulation;
    Gaussian noise models sensor variance.
    """
    c1 = base_c1  + 3.0 * math.sin(phase)       + random.gauss(0, 1.0 * noise_scale)
    c2 =  2.0      + 2.5 * math.sin(phase + 0.5) + random.gauss(0, 0.8 * noise_scale)
    c3 = -1.0      + 1.0 * math.sin(phase + 1.0) + random.gauss(0, 0.5 * noise_scale)
    c4 =  0.5      + 0.8 * math.cos(phase)        + random.gauss(0, 0.4 * noise_scale)
    c5 = -0.3      + 0.5 * math.cos(phase + 0.5) + random.gauss(0, 0.3 * noise_scale)
    return [round(c1, 3), round(c2, 3), round(c3, 3), round(c4, 3), round(c5, 3)]


def simulate_reading(beehive_id: int, when: datetime,
                     include_mfcc: bool = True,
                     overrides: dict = None,
                     light_mode: str = "auto",
                     point_idx: int = 0) -> dict:
    """Generate a realistic reading with sinusoidal drift + noise."""
    hour  = when.hour + when.minute / 60
    phase = 2 * math.pi * hour / 24

    temp_int = TEMP_BASE + TEMP_AMP * math.sin(phase)   + random.gauss(0, 0.3)
    hum_int  = HUM_BASE  - HUM_AMP  * math.sin(phase)   + random.gauss(0, 1.0)
    temp_ext = 15.0      + 8.0      * math.sin(phase)   + random.gauss(0, 0.5)
    hum_ext  = 70.0      - 20.0     * math.sin(phase)   + random.gauss(0, 2.0)

    sf_int = SOUND_FREQ_BASE + SOUND_FREQ_AMP * math.sin(phase) + random.gauss(0, 8)
    sa_int = SOUND_AMP_BASE  + SOUND_AMP_AMP  * math.sin(phase) + random.gauss(0, 0.03)
    sf_ext = 120.0 + 30.0 * math.sin(phase) + random.gauss(0, 10)
    sa_ext = 0.10  + 0.05 * math.sin(phase) + random.gauss(0, 0.02)

    if light_mode == "day":
        light = 85.0 + random.gauss(0, 2)
    elif light_mode == "night":
        light = 2.0 + random.gauss(0, 0.5)
    elif light_mode == "spike":
        # Alternate: 10 points high (95%), 10 points low (2%)
        light = 95.0 if (point_idx // 10) % 2 == 0 else 2.0
        light += random.gauss(0, 1)
    else:
        light = max(0.0, min(100.0,
            LIGHT_DAY_MAX * math.sin(math.pi * hour / 24) + random.gauss(0, 2)))

    light = max(0.0, min(100.0, round(light, 1)))

    payload = {
        "beehive_id":      beehive_id,
        "temperature_int": round(temp_int, 2),
        "humidity_int":    round(max(0, min(100, hum_int)), 2),
        "temperature_ext": round(temp_ext, 2),
        "humidity_ext":    round(max(0, min(100, hum_ext)), 2),
        "sound_freq_int":  round(max(0, sf_int), 2),
        "sound_amp_int":   round(max(0, min(1, sa_int)), 3),
        "sound_freq_ext":  round(max(0, sf_ext), 2),
        "sound_amp_ext":   round(max(0, min(1, sa_ext)), 3),
        "light_ext":       light,
        "timestamp":       when.strftime("%Y-%m-%dT%H:%M:%SZ"),
    }

    if include_mfcc:
        # Inside is noisier (bees, brood) → higher base energy (C1 ≈ -20)
        # Outside is quieter             → lower base energy  (C1 ≈ -25)
        payload["mfcc_int"] = _mfcc(phase, base_c1=-20.0)
        payload["mfcc_ext"] = _mfcc(phase, base_c1=-25.0, noise_scale=0.7)

    if overrides:
        for key, val in overrides.items():
            if key in payload:
                payload[key] = val

    return payload


def send(url: str, payload: dict) -> bool:
    try:
        r = requests.post(f"{url}/api/data", json=payload, timeout=5)
        if r.ok:
            mfcc_tag = " +MFCC" if "mfcc_int" in payload else ""
            print(f"  ✓  hive={payload['beehive_id']}  "
                  f"T={payload['temperature_int']}°C  "
                  f"H={payload['humidity_int']}%  "
                  f"f={payload['sound_freq_int']}Hz  "
                  f"@ {payload['timestamp']}{mfcc_tag}")
            return True
        print(f"  ✗  HTTP {r.status_code}: {r.text}")
        return False
    except requests.RequestException as e:
        print(f"  ✗  {e}")
        return False


def run_live(url, ids, interval, include_mfcc, overrides=None, light_mode="auto"):
    print(f"Live data → {url}  every {interval}s  MFCC={'yes' if include_mfcc else 'no'}  light={light_mode}  Ctrl+C to stop\n")
    counter = 0
    while True:
        now = datetime.now(timezone.utc)
        for bid in ids:
            send(url, simulate_reading(bid, now, include_mfcc, overrides, light_mode, point_idx=counter))
        counter += 1
        time.sleep(interval)


def run_burst(url, ids, points, include_mfcc, overrides=None, light_mode="auto"):
    """Inject `points` historical readings spread over the last 24 h."""
    print(f"Injecting {points} historical points per beehive → {url}  MFCC={'yes' if include_mfcc else 'no'}  light={light_mode}\n")
    step  = timedelta(hours=24) / points
    start = datetime.now(timezone.utc) - timedelta(hours=24)

    for bid in ids:
        print(f"Beehive {bid}:")
        ok = 0
        for i in range(points):
            when = start + step * i
            if send(url, simulate_reading(bid, when, include_mfcc, overrides, light_mode, point_idx=i)):
                ok += 1
        print(f"  → {ok}/{points} points written\n")


def main():
    p = argparse.ArgumentParser(description="Beetter sensor simulator")
    p.add_argument("--url",      default=DEFAULT_URL)
    p.add_argument("--ids",      nargs="+", type=int, default=DEFAULT_IDS, metavar="ID")
    p.add_argument("--interval", type=int, default=DEFAULT_INTERVAL, metavar="SEC")
    p.add_argument("--burst",    type=int, default=0, metavar="N")
    p.add_argument("--no-mfcc",  action="store_true",
                   help="Omit MFCC fields (simulates pre-firmware packets)")
    # Fixed value overrides — if provided, bypass simulation formula
    p.add_argument("--temp-int",   type=float, default=None, metavar="°C",  help="Fix temperature_int")
    p.add_argument("--temp-ext",   type=float, default=None, metavar="°C",  help="Fix temperature_ext")
    p.add_argument("--hum-int",    type=float, default=None, metavar="%",   help="Fix humidity_int")
    p.add_argument("--hum-ext",    type=float, default=None, metavar="%",   help="Fix humidity_ext")
    p.add_argument("--freq-int",   type=float, default=None, metavar="Hz",  help="Fix sound_freq_int")
    p.add_argument("--amp-int",    type=float, default=None, metavar="0-1", help="Fix sound_amp_int")
    p.add_argument("--freq-ext",   type=float, default=None, metavar="Hz",  help="Fix sound_freq_ext")
    p.add_argument("--amp-ext",    type=float, default=None, metavar="0-1", help="Fix sound_amp_ext")
    p.add_argument("--light",      type=float, default=None, metavar="%",   help="Fix light_ext")
    p.add_argument("--light-mode",
                   choices=["auto", "day", "night", "spike"],
                   default="auto",
                   help=(
                       "auto  = sinusoidal daily cycle (default)\n"
                       "day   = constant high light (~85%%)\n"
                       "night = constant low light (~2%%)\n"
                       "spike = oscillates between 0 and 95%% every 10 points "
                       "(tests threshold breach + recovery)"
                   ))
    args = p.parse_args()

    include_mfcc = not args.no_mfcc
    overrides = {k: v for k, v in {
        "temperature_int": args.temp_int,
        "temperature_ext": args.temp_ext,
        "humidity_int":    args.hum_int,
        "humidity_ext":    args.hum_ext,
        "sound_freq_int":  args.freq_int,
        "sound_amp_int":   args.amp_int,
        "sound_freq_ext":  args.freq_ext,
        "sound_amp_ext":   args.amp_ext,
        "light_ext":       args.light,
    }.items() if v is not None}

    if args.burst > 0:
        run_burst(args.url, args.ids, args.burst, include_mfcc, overrides, args.light_mode)
    else:
        run_live(args.url, args.ids, args.interval, include_mfcc, overrides, args.light_mode)


if __name__ == "__main__":
    main()
