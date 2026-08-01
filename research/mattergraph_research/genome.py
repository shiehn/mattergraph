"""Genome v0 = the macro parameters of the fixed v0 topology, Sobol-sampled.

A genome serializes directly to a SoundSkin JSON the renderer consumes. Search
space bounds deliberately avoid degenerate corners (zero decay, silent level).
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, asdict

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
    exciter_type: str = "noise_burst"  # "noise_burst" | "friction"
    to_level: float = 0.85
    to_brightness: float = 0.4
    to_hardness: float = 0.3
    roughness: float = 0.5
    grit_rate_hz: float = 90.0

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
]
_MODE_COUNT_RANGE = (4, 64)
_FRICTION_FRACTION = 0.3


def sobol_genomes(n: int, base_seed: int = 0) -> list[Genome]:
    dims = len(_CONTINUOUS) + 2  # + mode_count + exciter_type
    sampler = qmc.Sobol(d=dims, scramble=True, seed=base_seed)
    unit = sampler.random(n)
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
        values["mode_count"] = int(round(lo_m + float(row[-2]) * (hi_m - lo_m)))
        values["exciter_type"] = ("friction" if float(row[-1]) < _FRICTION_FRACTION
                                  else "noise_burst")
        values["skin_seed"] = base_seed * 1_000_003 + i
        out.append(Genome(**values))  # type: ignore[arg-type]
    return out
