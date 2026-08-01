"""Genome v0 = the macro parameters of the fixed v0 topology, Sobol-sampled.

A genome serializes directly to a SoundSkin JSON the renderer consumes. Search
space bounds deliberately avoid degenerate corners (zero decay, silent level).
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, asdict
from pathlib import Path

import numpy as np
from scipy.stats import qmc

SCHEMA_VERSION = "0.1.0"


@dataclass(frozen=True)
class Genome:
    mode_count: int
    inharmonicity: float
    brightness: float
    t60_base_s: float
    damping_slope: float
    irregularity: float
    position: float
    hardness: float
    color: float
    noisiness: float
    release_damp_factor: float
    stereo_spread: float
    skin_seed: int
    # v1 genes: velocity mappings are searched, not constants (behavior-contract
    # data showed vel->brightness inverted for 37% of the v0 space), and the
    # exciter gesture itself is a gene.
    exciter_type: str = "noise_burst"  # noise_burst | friction | periodic | sample
    to_level: float = 0.85
    to_brightness: float = 0.4
    to_hardness: float = 0.3
    roughness: float = 0.5
    grit_rate_hz: float = 90.0
    # v2 genes: periodic (wave/detune/drive), sample (bank file + blend).
    wave: float = 0.0
    detune_cents: float = 8.0
    drive: float = 0.3
    sample_file: str = ""
    sample_blend: float = 0.85

    def skin_json(self, name: str) -> str:
        skin = {
            "schema_version": SCHEMA_VERSION,
            "name": name,
            "skin_seed": self.skin_seed,
            "exciter": {
                "type": self.exciter_type,
                "hardness": round(self.hardness, 6),
                "color": round(self.color, 6),
                "level": 0.9,
                "noisiness": round(self.noisiness, 6),
                "roughness": round(self.roughness, 6),
                "grit_rate_hz": round(self.grit_rate_hz, 6),
                "wave": round(self.wave, 6),
                "detune_cents": round(self.detune_cents, 6),
                "drive": round(self.drive, 6),
                "sample_blend": round(self.sample_blend, 6),
                **({"sample": self.sample_file} if self.sample_file else {}),
            },
            "body": {
                "mode_count": self.mode_count,
                "inharmonicity": round(self.inharmonicity, 6),
                "brightness": round(self.brightness, 6),
                "t60_base_s": round(self.t60_base_s, 6),
                "damping_slope": round(self.damping_slope, 6),
                "irregularity": round(self.irregularity, 6),
                "position": round(self.position, 6),
            },
            "velocity": {
                "to_level": round(self.to_level, 6),
                "to_brightness": round(self.to_brightness, 6),
                "to_hardness": round(self.to_hardness, 6),
            },
            "release": {
                "mode": "damped",
                "damp_factor": round(self.release_damp_factor, 6),
            },
            "radiation": {
                "stereo_spread": round(self.stereo_spread, 6),
                "gain": 0.45,
            },
        }
        return json.dumps(skin, indent=1)

    def content_id(self) -> str:
        payload = json.dumps(asdict(self), sort_keys=True).encode()
        return "g_" + hashlib.sha256(payload).hexdigest()[:16]

    @staticmethod
    def from_skin_json(skin_json: str) -> "Genome":
        s = json.loads(skin_json)
        e, b, v, r, rad = (s["exciter"], s["body"], s.get("velocity", {}),
                           s.get("release", {}), s.get("radiation", {}))
        return Genome(
            mode_count=int(b["mode_count"]),
            inharmonicity=float(b["inharmonicity"]),
            brightness=float(b["brightness"]),
            t60_base_s=float(b["t60_base_s"]),
            damping_slope=float(b["damping_slope"]),
            irregularity=float(b["irregularity"]),
            position=float(b["position"]),
            hardness=float(e["hardness"]),
            color=float(e["color"]),
            noisiness=float(e.get("noisiness", 0.35)),
            release_damp_factor=float(r.get("damp_factor", 6.0)),
            stereo_spread=float(rad.get("stereo_spread", 0.5)),
            skin_seed=int(s["skin_seed"]),
            exciter_type=str(e.get("type", "noise_burst")),
            to_level=float(v.get("to_level", 0.85)),
            to_brightness=float(v.get("to_brightness", 0.4)),
            to_hardness=float(v.get("to_hardness", 0.3)),
            roughness=float(e.get("roughness", 0.5)),
            grit_rate_hz=float(e.get("grit_rate_hz", 90.0)),
            wave=float(e.get("wave", 0.0)),
            detune_cents=float(e.get("detune_cents", 8.0)),
            drive=float(e.get("drive", 0.3)),
            sample_file=str(e.get("sample", "")),
            sample_blend=float(e.get("sample_blend", 0.85)),
        )


# (name, low, high, log_scale)
_CONTINUOUS = [
    ("inharmonicity", 0.0, 1.0, False),
    ("brightness", 0.0, 1.0, False),
    ("t60_base_s", 0.05, 8.0, True),
    ("damping_slope", 0.0, 1.5, False),
    ("irregularity", 0.0, 0.6, False),
    ("position", 0.05, 0.95, False),
    ("hardness", 0.0, 1.0, False),
    ("color", 0.05, 1.0, False),
    ("noisiness", 0.0, 0.8, False),
    ("release_damp_factor", 1.0, 10.0, True),
    ("stereo_spread", 0.2, 0.8, False),
    ("to_level", 0.5, 1.0, False),
    ("to_brightness", 0.0, 1.0, False),
    ("to_hardness", 0.0, 1.0, False),
    ("roughness", 0.0, 1.0, False),
    ("grit_rate_hz", 10.0, 300.0, True),
    ("wave", 0.0, 1.0, False),
    ("detune_cents", 1.0, 30.0, True),
    ("drive", 0.0, 1.0, False),
    ("sample_blend", 0.5, 1.0, False),
]
_MODE_COUNT_RANGE = (4, 64)
# Exciter-type mix for sampling: strike-heavy, the three others equal-ish.
_TYPE_CUTS = [(0.45, "noise_burst"), (0.65, "friction"), (0.83, "periodic"),
              (1.01, "sample")]

_MANIFEST_PATH = Path(__file__).resolve().parents[2] / "assets/exciters/manifest.json"


def exciter_bank() -> list[str]:
    try:
        return [e["file"] for e in json.loads(_MANIFEST_PATH.read_text())]
    except (OSError, ValueError):
        return []


def _pick_type(u: float, bank: list[str]) -> str:
    for cut, name in _TYPE_CUTS:
        if u < cut:
            return name if (name != "sample" or bank) else "noise_burst"
    return "noise_burst"


def mutate(g: Genome, rng: np.random.Generator, mutation_rate: float = 0.35,
           sigma_frac: float = 0.15) -> Genome:
    """Gaussian mutation within bounds; occasional gesture flips and reseeds."""
    values = asdict(g)
    bank = exciter_bank()
    for name, lo, hi, log_scale in _CONTINUOUS:
        if rng.random() >= mutation_rate:
            continue
        v = float(values[name])
        if log_scale:
            lv = np.log(max(v, lo)) + rng.normal(0.0, sigma_frac * (np.log(hi) - np.log(lo)))
            values[name] = float(np.exp(np.clip(lv, np.log(lo), np.log(hi))))
        else:
            values[name] = float(np.clip(v + rng.normal(0.0, sigma_frac * (hi - lo)), lo, hi))
    if rng.random() < mutation_rate:
        lo_m, hi_m = _MODE_COUNT_RANGE
        values["mode_count"] = int(np.clip(g.mode_count + rng.integers(-8, 9), lo_m, hi_m))
    if rng.random() < 0.08:
        choices = ["noise_burst", "friction", "periodic"] + (["sample"] if bank else [])
        choices = [c for c in choices if c != g.exciter_type]
        values["exciter_type"] = choices[int(rng.integers(len(choices)))]
    if values["exciter_type"] == "sample":
        if not values.get("sample_file") or rng.random() < 0.15:
            values["sample_file"] = bank[int(rng.integers(len(bank)))] if bank else ""
        if not values["sample_file"]:
            values["exciter_type"] = "noise_burst"
    if rng.random() < 0.2:
        values["skin_seed"] = int(rng.integers(0, 2**31))  # new irregularity realization
    return Genome(**values)


def sobol_genomes(n: int, base_seed: int = 0) -> list[Genome]:
    dims = len(_CONTINUOUS) + 3  # + mode_count + exciter_type + sample pick
    sampler = qmc.Sobol(d=dims, scramble=True, seed=base_seed)
    unit = sampler.random(n)
    bank = exciter_bank()
    out: list[Genome] = []
    for i in range(n):
        row = unit[i]
        values: dict[str, float | int | str] = {}
        for j, (name, lo, hi, log_scale) in enumerate(_CONTINUOUS):
            u = float(row[j])
            if log_scale:
                values[name] = float(np.exp(np.log(lo) + u * (np.log(hi) - np.log(lo))))
            else:
                values[name] = lo + u * (hi - lo)
        lo_m, hi_m = _MODE_COUNT_RANGE
        values["mode_count"] = int(round(lo_m + float(row[-3]) * (hi_m - lo_m)))
        values["exciter_type"] = _pick_type(float(row[-2]), bank)
        values["sample_file"] = (bank[int(float(row[-1]) * len(bank)) % len(bank)]
                                 if values["exciter_type"] == "sample" and bank else "")
        values["skin_seed"] = base_seed * 1_000_003 + i
        out.append(Genome(**values))  # type: ignore[arg-type]
    return out
