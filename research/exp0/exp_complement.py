"""Complement-aware retrieval: fail-fast validation before app wiring.

Question: if retrieval factors in what the OTHER tracks sound like, does it
choose sensibly different, complementary skins — without betraying the prompt?

Method: two synthetic context beds with opposite characters:
    bed A "dark_pad":   sustained, low-band-heavy, no transients
    bed B "bright_hats": transient-dense, high-band-heavy
One neutral prompt ("a metallic percussion hit"). Retrieval pool = top-24 by
prompt cosine (relatability floor). Re-rank by:
    final = 0.60*prompt_rank + 0.25*spectral_complement + 0.15*temporal_complement
where spectral_complement = 1 - band-histogram intersection(skin, context) and
temporal_complement rewards the opposite transientness of the context.

PASS = the two beds select different top-1 skins from the same pool, shifted
in the predicted directions (dark bed -> brighter/higher-band pick;
busy bright bed -> darker and/or more sustained pick), and both picks remain
inside the prompt-relatable pool by construction.
"""

from __future__ import annotations

import base64
import json
from pathlib import Path

import numpy as np

from mattergraph_research.export_pack import GatewaySpaceEmbedder
from mattergraph_research.render import render_once

REPO = Path(__file__).resolve().parents[2]
PACK = REPO / "runs/mattergraph-skin-index.json"
SR = 48000

BAND_EDGES = np.array([60, 120, 250, 500, 1000, 2000, 4000, 8000, 16000], dtype=float)


def band_profile(x: np.ndarray) -> np.ndarray:
    """Energy distribution across 8 log bands (sums to 1)."""
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    freqs = np.fft.rfftfreq(len(x), 1 / SR)
    out = np.zeros(len(BAND_EDGES) - 1)
    for i in range(len(out)):
        m = (freqs >= BAND_EDGES[i]) & (freqs < BAND_EDGES[i + 1])
        out[i] = spec[m].sum()
    s = out.sum()
    return out / s if s > 0 else out


def transientness(x: np.ndarray) -> float:
    """0 = fully sustained, 1 = fully transient (envelope crest heuristic)."""
    env = np.convolve(np.abs(x), np.ones(240) / 240, mode="same")
    peak = env.max() + 1e-12
    return float(np.clip(1.0 - (env.mean() / peak) * 3.0, 0, 1))


def make_dark_pad() -> np.ndarray:
    t = np.arange(int(6 * SR)) / SR
    y = np.zeros_like(t)
    rng = np.random.default_rng(1)
    for f0 in (65.4, 98.0, 130.8):  # C2 stack, lows only
        for k in range(1, 6):
            y += (1.0 / k**1.5) * np.sin(2 * np.pi * f0 * k * t + rng.uniform(0, 6.28))
    return (0.7 * y / np.abs(y).max()).astype(np.float32)


def make_bright_hats() -> np.ndarray:
    rng = np.random.default_rng(2)
    y = np.zeros(int(6 * SR), dtype=np.float64)
    for n in range(48):  # 8 hits/sec
        at = int(n * SR / 8)
        burst = rng.standard_normal(2400) * np.exp(-np.arange(2400) / 300)
        hp = np.diff(burst, prepend=0.0)  # crude highpass -> bright
        y[at:at + 2400] += hp
    return (0.7 * y / np.abs(y).max()).astype(np.float32)


def main() -> None:
    pack = json.loads(PACK.read_text())
    emb = GatewaySpaceEmbedder()
    emb.health_check()

    prompt = "a metallic percussion hit"
    qv = emb.embed_text([prompt])[0]

    scored = []
    for s in pack["skins"]:
        best = max(
            float(np.frombuffer(base64.b64decode(b), dtype=np.float32) @ qv)
            for b in s["vecs_b64"])
        scored.append((best, s))
    scored.sort(key=lambda t: -t[0])
    pool = scored[:24]
    print(f"prompt pool: top-24 by cos ({pool[0][0]:.3f} .. {pool[-1][0]:.3f})")

    # Render each pool skin's strike+sustain probes once; measure bands+transientness.
    probes = [REPO / "fixtures/probes/diag_strike.clipspec.json",
              REPO / "fixtures/probes/diag_sustain.clipspec.json"]
    skin_stats = []
    for cos, s in pool:
        gj = json.dumps(s["genome"])
        mono = []
        for p in probes:
            out = render_once(gj, p, seed=3)
            if out.ok and out.audio is not None:
                mono.append(out.audio.reshape(-1, 2).mean(axis=1))
        if not mono:
            continue
        x = np.concatenate(mono)
        skin_stats.append({
            "name": s["name"], "cos": cos,
            "bands": band_profile(x), "trans": transientness(mono[0]),
            "centroid": s["features"]["centroid_hz"],
            "decay": s["features"]["decay_t60_s"],
        })

    beds = {"dark_pad": make_dark_pad(), "bright_hats": make_bright_hats()}
    results = {}
    for bed_name, bed in beds.items():
        ctx_bands = band_profile(bed.astype(np.float64))
        ctx_trans = transientness(bed.astype(np.float64))
        ranked = []
        n = len(skin_stats)
        for i, st in enumerate(sorted(skin_stats, key=lambda s: -s["cos"])):
            prompt_rank = 1.0 - i / max(n - 1, 1)
            spec_comp = 1.0 - float(np.minimum(st["bands"], ctx_bands).sum())
            temp_comp = 1.0 - abs(st["trans"] - (1.0 - ctx_trans))
            final = 0.60 * prompt_rank + 0.25 * spec_comp + 0.15 * temp_comp
            ranked.append((final, st, spec_comp, temp_comp))
        ranked.sort(key=lambda t: -t[0])
        top = ranked[0]
        results[bed_name] = top
        print(f"\n[{bed_name}] ctx_trans={ctx_trans:.2f} "
              f"low-band-mass={ctx_bands[:3].sum():.2f}")
        for final, st, sc, tc in ranked[:3]:
            print(f"   {st['name'][:26]:<26} final={final:.3f} cos={st['cos']:.3f} "
                  f"spec={sc:.2f} temp={tc:.2f} centroid={st['centroid']:.0f}Hz "
                  f"decay={st['decay']:.2f}s trans={st['trans']:.2f}")

    a, b = results["dark_pad"][1], results["bright_hats"][1]
    print("\n=== verdict ===")
    print(f"dark_pad pick:    {a['name']} (centroid {a['centroid']:.0f}Hz, trans {a['trans']:.2f})")
    print(f"bright_hats pick: {b['name']} (centroid {b['centroid']:.0f}Hz, trans {b['trans']:.2f})")
    different = a["name"] != b["name"]
    directional = a["centroid"] > b["centroid"] or b["trans"] < a["trans"]
    print(f"different picks: {different} | predicted direction: {directional}")
    print("PASS" if (different and directional) else "FAIL")


if __name__ == "__main__":
    main()
