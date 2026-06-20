"""
tools/simulate.py
─────────────────────────────────────────────────────────────────────────────
Beetter — sensor data simulator.

Sends fake readings to the local Flask API, mimicking what the LoRa receiver
would do in production.  Now includes synthetic MFCC coefficients so the full
9-feature ML pipeline can be exercised before the real hardware is deployed.

MFCC simulation strategy
─────────────────────────
The 13 coefficients evolve with a sinusoidal daily cycle + noise.
Their absolute values are calibrated against real 8 kHz librosa output
(sr=8000, fmin=50, fmax=2000, n_mfcc=13, averaged over a 3-second window):
  C0 (energy):       -55 to -35   (inside), -60 to -40 (outside)
  C1 (spectral tilt): -5 to +12
  C2–C12:             -6 to  +6   (decreasing amplitude for higher coefficients)

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
DEFAULT_IDS      = ["B001"]
DEFAULT_INTERVAL = 10

TEMP_BASE = 35.0;  TEMP_AMP = 2.0
HUM_BASE  = 60.0;  HUM_AMP  = 8.0
SOUND_FREQ_BASE = 230.0;  SOUND_FREQ_AMP = 40.0
SOUND_AMP_BASE  = 0.35;   SOUND_AMP_AMP  = 0.15
LIGHT_DAY_MAX   = 3.0

# ── State presets ─────────────────────────────────────────────────────────────
# Each preset overrides sensor values to mimic a known hive state.
# Individual --temp-int etc. flags still take priority over presets.
PRESETS = {
    'calm': {
        'temperature_int': 35.0,
        'temperature_ext': 20.0,
        'humidity_int':    63.0,
        'humidity_ext':    68.0,
        'sound_freq_int':  240.0,
        'sound_amp_int':   0.35,
        'sound_freq_ext':  115.0,
        'sound_amp_ext':   0.10,
        'light_ext':       1.5,
        '_description':    'Ruche calme — valeurs normales',
    },
    'warning': {
        'temperature_int': 38.5,
        'temperature_ext': 20.0,
        'humidity_int':    78.0,
        'humidity_ext':    72.0,
        'sound_freq_int':  340.0,
        'sound_amp_int':   0.65,
        'sound_freq_ext':  130.0,
        'sound_amp_ext':   0.12,
        'light_ext':       2.0,
        '_description':    'Ruche en alerte — dépassement seuil warn',
    },
    'critical': {
        'temperature_int': 43.0,
        'temperature_ext': 20.0,
        'humidity_int':    85.0,
        'humidity_ext':    72.0,
        'sound_freq_int':  390.0,
        'sound_amp_int':   0.90,
        'sound_freq_ext':  140.0,
        'sound_amp_ext':   0.15,
        'light_ext':       2.0,
        '_description':    'Ruche critique — dépassement seuil crit',
    },
    'swarming': {
        'temperature_int': 37.8,
        'temperature_ext': 20.0,
        'humidity_int':    70.0,
        'humidity_ext':    68.0,
        'sound_freq_int':  415.0,
        'sound_amp_int':   0.88,
        'sound_freq_ext':  120.0,
        'sound_amp_ext':   0.11,
        'light_ext':       1.8,
        '_description':    'Essaimage — fréquence haute, amplitude forte',
    },
    'predator': {
        'temperature_int': 36.0,
        'temperature_ext': 20.0,
        'humidity_int':    60.0,
        'humidity_ext':    68.0,
        'sound_freq_int':  370.0,
        'sound_amp_int':   0.97,
        'sound_freq_ext':  180.0,
        'sound_amp_ext':   0.25,
        'light_ext':       2.0,
        '_description':    'Prédateur — amplitude maximale, fréquence chaotique',
    },
}

PRESET_NOISE = {
    'temperature_int': 0.3,  'temperature_ext': 0.4,
    'humidity_int':    1.0,  'humidity_ext':    1.5,
    'sound_freq_int':  8.0,  'sound_amp_int':   0.03,
    'sound_freq_ext':  6.0,  'sound_amp_ext':   0.02,
    'light_ext':       0.2,
}


def _mfcc(phase, base_c0, noise_scale=1.0):
    """
    Generate a plausible 13-MFCC vector (indices 0–12) for one sensor channel.

    Ranges match real 8 kHz librosa output (sr=8000, fmin=50, fmax=2000, 3 s window):
      C0 (energy):  base_c0 ± 10 — inside base ≈ -45, outside base ≈ -50
      C1:           -5 to +12
      C2–C12:       -6 to +6, amplitude decreasing for higher coefficients
    """
    c0  = base_c0 + 10.0 * math.sin(phase)        + random.gauss(0, 2.0  * noise_scale)
    c1  =  3.5    +  8.5 * math.sin(phase + 0.5)  + random.gauss(0, 1.5  * noise_scale)
    c2  =  0.0    +  4.0 * math.sin(phase + 1.0)  + random.gauss(0, 1.0  * noise_scale)
    c3  =  0.0    +  3.5 * math.cos(phase)         + random.gauss(0, 0.8  * noise_scale)
    c4  =  0.0    +  3.0 * math.cos(phase + 0.5)  + random.gauss(0, 0.7  * noise_scale)
    c5  =  0.0    +  2.5 * math.sin(phase + 0.3)  + random.gauss(0, 0.6  * noise_scale)
    c6  =  0.0    +  2.0 * math.sin(phase + 0.7)  + random.gauss(0, 0.5  * noise_scale)
    c7  =  0.0    +  1.8 * math.cos(phase + 0.2)  + random.gauss(0, 0.45 * noise_scale)
    c8  =  0.0    +  1.5 * math.cos(phase + 0.8)  + random.gauss(0, 0.4  * noise_scale)
    c9  =  0.0    +  1.2 * math.sin(phase + 1.2)  + random.gauss(0, 0.35 * noise_scale)
    c10 =  0.0    +  1.0 * math.sin(phase + 0.6)  + random.gauss(0, 0.3  * noise_scale)
    c11 =  0.0    +  0.8 * math.cos(phase + 1.0)  + random.gauss(0, 0.25 * noise_scale)
    c12 =  0.0    +  0.6 * math.cos(phase + 0.4)  + random.gauss(0, 0.2  * noise_scale)
    return [round(x, 3) for x in [c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12]]


def simulate_reading(beehive_id: int, when: datetime,
                     include_mfcc: bool = True,
                     overrides: dict = None,
                     light_mode: str = "auto",
                     point_idx: int = 0,
                     add_noise_to_overrides: bool = False) -> dict:
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
        light = 8.5 + random.gauss(0, 0.2)
    elif light_mode == "night":
        light = 0.2 + random.gauss(0, 0.05)
    elif light_mode == "spike":
        # Alternate: 10 points high (~9.5), 10 points low (~0.2)
        light = 9.5 if (point_idx // 10) % 2 == 0 else 0.2
        light += random.gauss(0, 0.1)
    else:
        light = max(0.0, min(4.0, random.gauss(1.5, 0.5)))

    light = max(0.0, min(10.0, round(light, 1)))

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
        # Inside (bees, brood) → higher base energy (C0 ≈ -45)
        # Outside              → lower base energy  (C0 ≈ -50)
        payload["mfcc_int"] = _mfcc(phase, base_c0=-45.0)
        payload["mfcc_ext"] = _mfcc(phase, base_c0=-50.0, noise_scale=0.7)

    if overrides:
        for key, val in overrides.items():
            if key in payload:
                if add_noise_to_overrides and key in PRESET_NOISE:
                    noisy = val + random.gauss(0, PRESET_NOISE[key])
                    if 'humidity' in key:   noisy = max(0, min(100, noisy))
                    elif 'amp' in key:      noisy = max(0, min(1, noisy))
                    elif 'light' in key:    noisy = max(0, min(10, noisy))
                    else:                   noisy = max(0, noisy)
                    payload[key] = round(noisy, 2)
                else:
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


def run_live(url, ids, interval, include_mfcc, overrides=None, light_mode="auto",
             add_noise_to_overrides=False):
    print(f"Live data → {url}  every {interval}s  MFCC={'yes' if include_mfcc else 'no'}  light={light_mode}  Ctrl+C to stop\n")
    counter = 0
    while True:
        now = datetime.now(timezone.utc)
        for bid in ids:
            send(url, simulate_reading(bid, now, include_mfcc, overrides, light_mode,
                                       point_idx=counter,
                                       add_noise_to_overrides=add_noise_to_overrides))
        counter += 1
        time.sleep(interval)


def run_burst(url, ids, points, include_mfcc, overrides=None, light_mode="auto",
              add_noise_to_overrides=False):
    """Inject `points` historical readings spread over the last 24 h."""
    print(f"Injecting {points} historical points per beehive → {url}  MFCC={'yes' if include_mfcc else 'no'}  light={light_mode}\n")
    step  = timedelta(hours=24) / points
    start = datetime.now(timezone.utc) - timedelta(hours=24)

    for bid in ids:
        print(f"Beehive {bid}:")
        ok = 0
        for i in range(points):
            when = start + step * i
            if send(url, simulate_reading(bid, when, include_mfcc, overrides, light_mode,
                                          point_idx=i,
                                          add_noise_to_overrides=add_noise_to_overrides)):
                ok += 1
        print(f"  → {ok}/{points} points written\n")


def main():
    p = argparse.ArgumentParser(description="Beetter sensor simulator")
    p.add_argument("--url",      default=DEFAULT_URL)
    p.add_argument("--ids",      nargs="+", type=str, default=DEFAULT_IDS, metavar="ID")
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
    p.add_argument("--light",      type=float, default=None, metavar="0-10", help="Fix light_ext (0–10 scale)")
    p.add_argument("--light-mode",
                   choices=["auto", "day", "night", "spike"],
                   default="auto",
                   help=(
                       "auto  = sinusoidal daily cycle (default)\n"
                       "day   = constant high light (~8.5/10)\n"
                       "night = constant low light (~0.2/10)\n"
                       "spike = oscillates between 0 and 9.5 every 10 points "
                       "(tests threshold breach + recovery)"
                   ))
    p.add_argument(
        '--preset',
        choices=list(PRESETS.keys()),
        default=None,
        metavar='STATE',
        help=(
            'Preset state shortcut. Overrides simulation formulas with realistic values '
            'for the given state. Individual --temp-int etc. flags still take priority.\n'
            'Available: ' + ', '.join(PRESETS.keys())
        )
    )
    args = p.parse_args()

    include_mfcc = not args.no_mfcc
    cli_overrides = {k: v for k, v in {
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

    if args.preset:
        preset_vals = {k: v for k, v in PRESETS[args.preset].items()
                       if not k.startswith('_')}
        overrides = {**preset_vals, **cli_overrides}
        print(f"  Preset: {args.preset} — {PRESETS[args.preset]['_description']}")
        if cli_overrides:
            print(f"  Overrides: {cli_overrides}")
    else:
        overrides = cli_overrides

    add_noise = bool(args.preset)
    if args.burst > 0:
        run_burst(args.url, args.ids, args.burst, include_mfcc, overrides, args.light_mode,
                  add_noise_to_overrides=add_noise)
    else:
        run_live(args.url, args.ids, args.interval, include_mfcc, overrides, args.light_mode,
                 add_noise_to_overrides=add_noise)


if __name__ == "__main__":
    main()
