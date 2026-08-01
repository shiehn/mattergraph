"""Behavior contracts v0: velocity response, scored across the atlas.

For each skin, render the velocity-ladder probe and measure how per-note RMS
and spectral centroid track velocity (Spearman rho). The plan's §12.4 contract
("louder and brighter when struck harder") becomes two numbers per skin, stored
in the atlas and usable as retrieval filters.

Run: python -m mattergraph_research.contracts --atlas ../runs/c0.sqlite
"""

from __future__ import annotations

import argparse
import json
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

from .atlas import Atlas
from .render import render_once

REPO = Path(__file__).resolve().parents[2]
LADDER = REPO / "fixtures/probes/velocity_ladder.clipspec.json"

SCHEMA = """
CREATE TABLE IF NOT EXISTS behavior (
  skin_id TEXT PRIMARY KEY REFERENCES skins(id),
  vel_rms_rho REAL, vel_centroid_rho REAL, ok INTEGER
);
"""


def spearman(x: np.ndarray, y: np.ndarray) -> float:
    rx = np.argsort(np.argsort(x)).astype(float)
    ry = np.argsort(np.argsort(y)).astype(float)
    rx -= rx.mean()
    ry -= ry.mean()
    denom = np.sqrt((rx**2).sum() * (ry**2).sum())
    return float((rx * ry).sum() / denom) if denom > 0 else 0.0


def note_windows() -> list[tuple[int, int]]:
    spec = json.loads(LADDER.read_text())
    spq = 60.0 / spec["bpm"] * 48000
    return [(int(round(n["start_qn"] * spq)),
             int(round((n["start_qn"] + n["dur_qn"]) * spq)))
            for n in spec["notes"]]


def score_skin(skin_json: str) -> tuple[float, float, bool]:
    out = render_once(skin_json, LADDER, seed=42)
    if not out.ok or out.audio is None:
        return 0.0, 0.0, False
    x = out.audio.reshape(-1, 2).mean(axis=1).astype(np.float64)
    vels = np.array([16, 32, 48, 64, 80, 96, 112, 127], dtype=float)
    rms_list, cent_list = [], []
    for a, b in note_windows():
        seg = x[a:min(b, len(x))]
        rms_list.append(float(np.sqrt(np.mean(seg**2) + 1e-18)))
        mags = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
        freqs = np.fft.rfftfreq(len(seg), d=1 / 48000)
        cent_list.append(float((mags * freqs).sum() / (mags.sum() + 1e-12)))
    return spearman(vels, np.array(rms_list)), spearman(vels, np.array(cent_list)), True


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--atlas", type=Path, required=True)
    ap.add_argument("--workers", type=int, default=8)
    args = ap.parse_args()

    atlas = Atlas(args.atlas)
    atlas.conn.executescript(SCHEMA)
    skins = atlas.conn.execute("SELECT id, genome_json FROM skins").fetchall()

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        results = list(pool.map(lambda row: score_skin(row[1]), skins))
    for (sid, _), (r_rms, r_cent, ok) in zip(skins, results):
        atlas.conn.execute("INSERT OR REPLACE INTO behavior VALUES (?,?,?,?)",
                           (sid, r_rms, r_cent, int(ok)))
    atlas.commit()

    rms_rhos = np.array([r for r, _, ok in results if ok])
    cent_rhos = np.array([c for _, c, ok in results if ok])
    print(f"scored {len(rms_rhos)}/{len(skins)} skins in {time.time() - t0:.0f}s")
    print(f"vel->RMS rho:      median {np.median(rms_rhos):+.2f}, "
          f">=0.9: {(rms_rhos >= 0.9).mean():.0%}, <0: {(rms_rhos < 0).mean():.1%}")
    print(f"vel->centroid rho: median {np.median(cent_rhos):+.2f}, "
          f">=0.7: {(cent_rhos >= 0.7).mean():.0%}, <0: {(cent_rhos < 0).mean():.1%}")


if __name__ == "__main__":
    main()
