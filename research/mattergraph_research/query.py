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
    # Struck-wood words imply wooden brevity (marimba served a 1s clap hybrid).
    (("knock", "marimba", "woodblock", "xylophone", "mallet"), "decay_t60_s", "<", 0.8),
    (("ring", "ringing", "sustain", "sustained", "chime", "bell", "bowl", "gong"),
     "decay_t60_s", ">", 1.0),
    (("deep", "dark", "muffled", "thump", "dull"), "centroid_hz", "<", 950.0),
    # "chime" joined the bright pole: chimes are bright by definition (a 542 Hz
    # "delicate glass chime" top-1 failed human audition).
    (("bright", "shimmering", "sparkling", "brilliant", "icy", "chime", "chimes",
      "delicate"), "centroid_hz", ">", 1100.0),
]

# Percussiveness axis: calibrated against NSynth human labels, AUC 0.995
# (exp0/axis_calibration.json). Blind human eval confirmed the need: a friction
# skin topped "glass chime" and was rejected by ear. Strike-words demand
# percussive skins; sustain-words demand the opposite.
PERCUSSIVE_POS = ["a percussive plucked or struck sound with a sharp attack",
                  "a hit with a hard transient"]
PERCUSSIVE_NEG = ["a smooth sustained tone with a soft slow attack",
                  "a flowing continuous sound"]
STRIKE_WORDS = ("percussion", "percussive", "struck", "strike", "hit", "knock",
                "chime", "click", "pluck", "plucked", "mallet", "drum", "thump",
                "tap", "staccato", "marimba", "xylophone", "bell")
SUSTAIN_WORDS = ("bowed", "bowing", "drone", "scrape", "scraped", "scraping",
                 "sustained", "pad", "bow", "friction")


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

    # Gesture gating via the calibrated percussiveness axis.
    words = set(prompt.lower().replace(",", " ").split())
    wants_strike = bool(words & set(STRIKE_WORDS))
    wants_sustain = bool(words & set(SUSTAIN_WORDS))
    if wants_strike != wants_sustain:  # unambiguous gesture request
        axis = (embedder.embed_text(PERCUSSIVE_POS).mean(axis=0)
                - embedder.embed_text(PERCUSSIVE_NEG).mean(axis=0))
        axis_sims = mat @ axis
        skin_axis: dict[str, float] = {}
        for sid, a in zip(skin_ids, axis_sims):
            if a > skin_axis.get(sid, -2.0):
                skin_axis[sid] = float(a)
        cut = float(np.median(list(skin_axis.values())))
        keep = {sid for sid, a in skin_axis.items()
                if (a >= cut) == wants_strike}
        gated = {sid: s for sid, s in best.items() if sid in keep}
        if gated:  # never let the gate empty the pool silently
            best = gated

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

    # Anchor-first tie preference (plan rev4 §0 change 10): hand-tuned anchors
    # and human-kept curated skins outrank a random genome when scores are
    # within epsilon — the quality floor should win ties, and retrieval top-1s
    # stop re-rolling every time the atlas grows.
    EPSILON = 0.02
    if filtered:
        top_score = filtered[0][1]
        filtered.sort(key=lambda kv: (
            -(kv[1] + (EPSILON if kv[0].startswith("anchor_")
                       and kv[1] >= top_score - EPSILON else 0.0)),
        ))

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
