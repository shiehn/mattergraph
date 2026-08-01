"""Held-out prompt set for the Phase-2 retrieval gate.

Each rule-carrying prompt encodes machine-checkable feature assertions, so the
gate is quantitative without a human in the loop: retrieval's top-k hit-rate
must beat the corpus base rate (= random selection's expected hit-rate).

Rule format: (feature, op, value). Features ending in ``_pct`` are corpus
percentile ranks in [0, 1]; others are absolute values from features.py.
"""

from __future__ import annotations

Rule = tuple[str, str, float]

HELD_OUT: list[tuple[str, list[Rule]]] = [
    # Glass / chime family: ringing and comparatively bright.
    ("a delicate glass chime with a clear ring", [("decay_t60_s", ">", 1.0), ("centroid_pct", ">", 0.5)]),
    ("glassy crystalline percussion, bright and fragile", [("decay_t60_s", ">", 0.8), ("centroid_pct", ">", 0.55)]),
    ("struck wine glass, pure and ringing", [("decay_t60_s", ">", 1.2)]),
    # Wood family: short, darker.
    ("a warm wooden knock, like a marimba", [("decay_t60_s", "<", 0.7), ("centroid_pct", "<", 0.5)]),
    ("dry woodblock hit, short and mellow", [("decay_t60_s", "<", 0.5), ("centroid_pct", "<", 0.55)]),
    ("soft wooden mallet on a xylophone bar", [("decay_t60_s", "<", 0.8)]),
    # Metal / bell family: long inharmonic ring.
    ("a metal bell with a long sustained ring", [("decay_t60_s", ">", 2.5)]),
    ("metallic clang with slowly fading overtones", [("decay_t60_s", ">", 2.0)]),
    ("a small bronze gong, ringing on and on", [("decay_t60_s", ">", 2.5)]),
    # Drum / membrane family: low and short.
    ("a deep drum thump", [("centroid_pct", "<", 0.35), ("decay_t60_s", "<", 1.2)]),
    ("low tom hit, round and quick", [("centroid_pct", "<", 0.4), ("decay_t60_s", "<", 1.0)]),
    ("muffled kick drum knock", [("centroid_pct", "<", 0.3)]),
    # Brightness poles.
    ("an extremely bright sparkling tone", [("centroid_pct", ">", 0.7)]),
    ("brilliant shimmering high-frequency strike", [("centroid_pct", ">", 0.7)]),
    ("a dark muffled dull hit with no highs", [("centroid_pct", "<", 0.3)]),
    ("deep dark lowpassed thud", [("centroid_pct", "<", 0.3)]),
    # Decay poles.
    ("short staccato click that stops immediately", [("decay_t60_s", "<", 0.35)]),
    ("tight dry percussive tick", [("decay_t60_s", "<", 0.4)]),
    ("endless ringing resonance, very long decay", [("decay_t60_s", ">", 3.0)]),
    ("a tone that sustains and fades very slowly", [("decay_t60_s", ">", 3.0)]),
    # Spectral texture.
    ("harsh noisy metallic scrape", [("hf_ratio_pct", ">", 0.6)]),
    ("airy bright attack with lots of high end", [("hf_ratio_pct", ">", 0.6)]),
    ("smooth warm rounded tone", [("hf_ratio_pct", "<", 0.4)]),
    ("mellow soft-spoken thump", [("hf_ratio_pct", "<", 0.45)]),
    # Open prompts: scored by CLAP similarity lift only (no feature rules).
    ("shimmering icy texture", []),
    ("hollow tube being tapped", []),
    ("a tiny music box note", []),
    ("gong wash in a temple", []),
    ("dry click of a typewriter key", []),
    ("resonant singing bowl", []),
]
