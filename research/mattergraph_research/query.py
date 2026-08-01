"""Retrieval v0.2: best-probe-match CLAP scoring + transparent constraint rerank.

Scoring: a skin's score for a prompt is the MAX cosine over its per-probe render
embeddings (mid-register strike, arp, low-register strike) — the sas-patch-service
register lesson. Mean-pooling washed out register identity ("deep drum thump"
could never match an atlas that only sounded C4).

Constraints: a small, transparent keyword→feature map applies HARD filters on
the CLAP shortlist (decay for staccato/ringing words, centroid for deep/bright
words). This is an explicit stopgap for the real SoundSpec parser (plan §4);
the keyword table is deliberately tiny and auditable.
"""

from __future__ import annotations

import numpy as np

from .atlas import Atlas
from .clap_embed import ClapEmbedder

# (trigger words, feature, op, threshold) — stopgap until SoundSpec parsing.
CONSTRAINT_HINTS: list[tuple[tuple[str, ...], str, str, float]] = [
    (("staccato", "click", "tick", "dry", "short"), "decay_t60_s", "<", 0.5),
    (("ring", "ringing", "sustain", "sustained", "chime", "bell", "bowl", "gong"),
     "decay_t60_s", ">", 1.0),
    (("deep", "dark", "muffled", "thump", "dull"), "centroid_hz", "<", 950.0),
    (("bright", "shimmering", "sparkling", "brilliant", "icy"), "centroid_hz", ">", 1300.0),
]


def prompt_constraints(prompt: str) -> list[tuple[str, str, float]]:
    words = set(prompt.lower().replace(",", " ").split())
    out = []
    for triggers, feature, op, value in CONSTRAINT_HINTS:
        if words & set(triggers):
            out.append((feature, op, value))
    return out


def search_skins(atlas: Atlas, embedder: ClapEmbedder, prompt: str,
                 topk: int = 6, shortlist: int = 64) -> list[dict]:
    skin_ids, mat = atlas.render_matrix()
    if not skin_ids:
        return []
    qv = embedder.embed_text([prompt])[0]
    sims = mat @ qv

    best: dict[str, float] = {}
    for sid, s in zip(skin_ids, sims):
        if s > best.get(sid, -2.0):
            best[sid] = float(s)
    ranked = sorted(best.items(), key=lambda kv: -kv[1])[:shortlist]

    feats = atlas.skin_features()
    constraints = prompt_constraints(prompt)

    def ok(sid: str) -> bool:
        f = feats.get(sid)
        if f is None:
            return False
        for feature, op, value in constraints:
            v = f.get(feature, 0.0)
            if op == "<" and not v < value:
                return False
            if op == ">" and not v > value:
                return False
        return True

    filtered = [(sid, s) for sid, s in ranked if ok(sid)] if constraints else ranked
    if not filtered:  # constraints too strict for this atlas: disclose by falling back
        filtered = ranked

    out = []
    for sid, s in filtered[:topk]:
        f = feats.get(sid, {})
        out.append({
            "skin_id": sid,
            "cos": s,
            "decay_t60_s": round(f.get("decay_t60_s", 0), 2),
            "centroid_hz": int(f.get("centroid_hz", 0)),
            "constraints": [f"{a}{op}{v:g}" for a, op, v in constraints],
        })
    return out
