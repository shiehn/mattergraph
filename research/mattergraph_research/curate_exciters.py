"""Curate the sample-exciter bank from the owned S&S drum pack (v3).

Selects transient-rich one-shots, onset-aligns, trims to <=250 ms, resamples to
48 kHz mono float32, peak-normalizes, content-hashes, and writes:

    assets/exciters/<role>-<hash8>.wav      (gitignored — owned content stays staged)
    assets/exciters/manifest.json           (tracked — hashes, roles, prompt sidecars)

The generation-prompt sidecars ride along as semantic labels: the bank arrives
pre-labeled for retrieval. Provenance: pack id + version recorded per entry.

Run: python -m mattergraph_research.curate_exciters \
         --pack ../runs/drum-pack/pack --per-role 6
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

REPO = Path(__file__).resolve().parents[2]
OUT_DIR = REPO / "assets/exciters"

ROLES = ["kick", "snare-standard", "snare-rim", "clap", "hat-closed", "shaker",
         "foley-perc", "tom-low", "impact", "zap"]

TARGET_SR = 48_000
MAX_MS = 250
FADE_MS = 15
ONSET_DB = -40.0


def curate_one(path: Path) -> tuple[np.ndarray, float] | None:
    x, sr = sf.read(path, dtype="float32", always_2d=True)
    x = x.mean(axis=1)
    if sr != TARGET_SR:
        x = resample_poly(x, TARGET_SR, sr).astype(np.float32)
    peak = float(np.abs(x).max())
    if peak < 1e-4:
        return None
    onset_gate = peak * 10 ** (ONSET_DB / 20)
    above = np.nonzero(np.abs(x) >= onset_gate)[0]
    if len(above) == 0:
        return None
    start = max(0, int(above[0]) - 24)  # keep 0.5 ms pre-onset
    x = x[start:start + int(TARGET_SR * MAX_MS / 1000)]
    fade = int(TARGET_SR * FADE_MS / 1000)
    if len(x) > fade:
        x[-fade:] *= 0.5 * (1 + np.cos(np.pi * np.arange(fade) / fade))
    x = (0.9 * x / (np.abs(x).max() + 1e-9)).astype(np.float32)
    return x, len(x) / TARGET_SR


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", type=Path, required=True)
    ap.add_argument("--per-role", type=int, default=6)
    args = ap.parse_args()

    version = json.loads((args.pack / "_pack-version.json").read_text())
    provenance = {"packId": version.get("packId", "sas-drum-pack"),
                  "version": version.get("version"),
                  "sourceCommit": version.get("sourceCommit")}

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    manifest: list[dict] = []
    for role in ROLES:
        role_dir = args.pack / role
        if not role_dir.exists():
            print(f"[curate] role missing in pack: {role}")
            continue
        kept = 0
        # Deterministic pick: lexicographic order, evenly strided across the role.
        candidates = sorted(role_dir.glob("*.wav"))
        stride = max(1, len(candidates) // (args.per_role * 3))
        for path in candidates[::stride]:
            if kept >= args.per_role:
                break
            out = curate_one(path)
            if out is None:
                continue
            x, dur = out
            digest = hashlib.sha256(x.tobytes()).hexdigest()
            name = f"{role}-{digest[:8]}.wav"
            sf.write(OUT_DIR / name, x, TARGET_SR, subtype="FLOAT")
            sidecar = path.with_suffix(".txt")
            prompt = sidecar.read_text().strip() if sidecar.exists() else ""
            manifest.append({
                "file": name, "role": role, "sha256": digest,
                "duration_s": round(dur, 4), "prompt": prompt,
                "source": path.name, **provenance,
            })
            kept += 1
        print(f"[curate] {role}: kept {kept}")

    (OUT_DIR / "manifest.json").write_text(json.dumps(manifest, indent=1))
    print(f"[curate] bank: {len(manifest)} exciters -> {OUT_DIR}")


if __name__ == "__main__":
    main()
