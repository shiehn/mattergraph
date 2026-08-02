"""Mine single-cycle wavetables from the owned instrument pack.

The pack's prompt:sound key-value structure (each instrument's manifest
carries its generation prompt and per-zone root_midi) makes extraction exact:
the period is sr/f0 with f0 known from root_midi — no pitch detection needed.
One cycle from the sustained region, resampled to 2048 samples, end-blended
for a seamless loop, prompt label preserved in the bank manifest.

Run: python -m mattergraph_research.curate_wavetables --per-category 3
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[2]
OUT_DIR = REPO / "assets/wavetables"
PACK = Path.home() / "Library/Application Support/signals-and-sorcery/sample-packs/instruments"

TONAL_CATEGORIES = [
    "basses", "reese-bass", "808-bass", "bells", "keys", "pads", "plucks",
    "leads", "organs", "strings", "brass", "accordion", "banjos", "flutes",
    "guitars", "pianos", "synths",
]
TABLE_LEN = 2048


def extract_cycle(wav_path: Path, root_midi: int) -> np.ndarray | None:
    try:
        x, sr = sf.read(wav_path, dtype="float64", always_2d=True)
    except Exception:
        return None
    x = x.mean(axis=1)
    f0 = 440.0 * 2 ** ((root_midi - 69) / 12)
    period = sr / f0
    if period < 8 or period > sr:
        return None
    # Sustained region: skip the attack quarter, need 4 periods of runway.
    start = int(len(x) * 0.25)
    if start + 4 * period > len(x):
        start = max(0, int(len(x) * 0.1))
        if start + 2 * period > len(x):
            return None
    # Upward zero crossing for a clean seam.
    seg = x[start:start + int(3 * period)]
    crossings = np.nonzero((seg[:-1] <= 0) & (seg[1:] > 0))[0]
    at = start + (int(crossings[0]) if len(crossings) else 0)
    src = x[at:at + int(np.ceil(period)) + 2]
    if len(src) < 8:
        return None
    # Resample exactly one period to TABLE_LEN.
    pos = np.linspace(0.0, period, TABLE_LEN, endpoint=False)
    cycle = np.interp(pos, np.arange(len(src)), src)
    # Seam blend: crossfade the last 3% into the start.
    blend = TABLE_LEN // 32
    w = np.linspace(0, 1, blend)
    cycle[-blend:] = cycle[-blend:] * (1 - w) + cycle[:blend][::-1] * 0  # fade tail
    cycle[-blend:] += cycle[0] * w * 0.5
    peak = np.abs(cycle).max()
    if peak < 1e-4:
        return None
    return (0.9 * cycle / peak).astype(np.float32)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-category", type=int, default=3)
    args = ap.parse_args()

    if not PACK.exists():
        raise SystemExit(f"instrument pack not installed at {PACK}")
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    available = {d.name for d in PACK.iterdir() if d.is_dir()}
    manifest: list[dict] = []
    for cat in TONAL_CATEGORIES:
        if cat not in available:
            continue
        kept = 0
        for inst_dir in sorted((PACK / cat).iterdir()):
            if kept >= args.per_category or not inst_dir.is_dir():
                continue
            mpath = inst_dir / "manifest.json"
            if not mpath.exists():
                continue
            m = json.loads(mpath.read_text())
            zones = m.get("zones") or m.get("sources") or []
            if not zones:
                continue
            zone = min(zones, key=lambda z: abs(int(z.get("root_midi", 60)) - 48))
            wav = inst_dir / zone["sample"]
            cycle = extract_cycle(wav, int(zone.get("root_midi", 48)))
            if cycle is None:
                continue
            digest = hashlib.sha256(cycle.tobytes()).hexdigest()
            name = f"{cat}-{digest[:8]}.wav"
            sf.write(OUT_DIR / name, cycle, 48000, subtype="FLOAT")
            manifest.append({
                "file": name, "category": cat, "sha256": digest,
                "prompt": m.get("prompt", ""),
                "instrument_id": m.get("instrument_id", inst_dir.name),
                "root_midi": int(zone.get("root_midi", 48)),
            })
            kept += 1
        if kept:
            print(f"[wt] {cat}: {kept} tables")
    (OUT_DIR / "manifest.json").write_text(json.dumps(manifest, indent=1))
    print(f"[wt] bank: {len(manifest)} wavetables -> {OUT_DIR}")


if __name__ == "__main__":
    main()
