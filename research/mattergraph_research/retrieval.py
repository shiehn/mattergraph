"""Text -> SoundSkin retrieval, plus the Phase-2 held-out evaluation gate.

Retrieval: prompt -> CLAP text embedding -> cosine over skin embeddings.
Evaluation (python -m mattergraph_research.retrieval --atlas runs/c0.sqlite):

  1. Rule hit-rate: for each rule-carrying held-out prompt, the fraction of
     retrieval's top-k whose acoustic features satisfy the prompt's assertions,
     against the corpus base rate (= expected hit-rate of random selection).
  2. Semantic lift: mean audio-text cosine of top-k vs corpus mean, for every
     prompt. Retrieval uses only text->text-side geometry, so beating the
     corpus mean demonstrates real text->audio transfer over the atlas.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

from .atlas import Atlas
from .clap_embed import ClapEmbedder
from .heldout import HELD_OUT


def percentile_ranks(values: dict[str, float]) -> dict[str, float]:
    keys = list(values)
    arr = np.array([values[k] for k in keys])
    order = arr.argsort().argsort()
    return {k: float(order[i]) / max(len(keys) - 1, 1) for i, k in enumerate(keys)}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--atlas", type=Path, required=True)
    ap.add_argument("--topk", type=int, default=3)
    args = ap.parse_args()

    atlas = Atlas(args.atlas)
    ids, mat = atlas.skin_matrix()
    if not ids:
        raise SystemExit("atlas has no skin embeddings")
    feats = atlas.skin_features()

    # Percentile features across the corpus, for _pct rules.
    pct_bases = {"centroid_pct": "centroid_hz", "hf_ratio_pct": "hf_ratio"}
    pct: dict[str, dict[str, float]] = {sid: {} for sid in ids}
    for pct_key, base in pct_bases.items():
        ranks = percentile_ranks({sid: feats[sid][base] for sid in ids if sid in feats})
        for sid, r in ranks.items():
            pct[sid][pct_key] = r

    def skin_value(sid: str, feature: str) -> float:
        if feature.endswith("_pct"):
            return pct[sid].get(feature, 0.0)
        return feats[sid][feature]

    def satisfies(sid: str, rules) -> bool:
        for feature, op, value in rules:
            v = skin_value(sid, feature)
            if op == ">" and not v > value:
                return False
            if op == "<" and not v < value:
                return False
        return True

    embedder = ClapEmbedder()
    embedder.health_check()
    text_vecs = embedder.embed_text([p for p, _ in HELD_OUT])
    sims = text_vecs @ mat.T  # (prompts, skins)

    k = args.topk
    rule_hits, base_rates, lifts = [], [], []
    print(f"\n{'prompt':<52} {'top-k hit':>9} {'base':>6} {'sem lift':>9}")
    for row, (prompt, rules) in enumerate(HELD_OUT):
        top = np.argsort(-sims[row])[:k]
        top_ids = [ids[i] for i in top]
        sem_lift = float(sims[row][top].mean() - sims[row].mean())
        lifts.append(sem_lift)
        if rules:
            valid = [sid for sid in ids if sid in feats]
            hit = float(np.mean([satisfies(sid, rules) for sid in top_ids if sid in feats] or [0.0]))
            base = float(np.mean([satisfies(sid, rules) for sid in valid]))
            rule_hits.append(hit)
            base_rates.append(base)
            print(f"{prompt:<52} {hit:>9.2f} {base:>6.2f} {sem_lift:>+9.3f}")
        else:
            print(f"{prompt:<52} {'--':>9} {'--':>6} {sem_lift:>+9.3f}")

    print("\n=== Phase-2 gate summary ===")
    print(f"skins in atlas: {len(ids)}")
    print(f"rule prompts: mean top-{k} hit-rate {np.mean(rule_hits):.3f} "
          f"vs random baseline {np.mean(base_rates):.3f} "
          f"(lift x{np.mean(rule_hits) / max(np.mean(base_rates), 1e-9):.2f})")
    print(f"semantic lift (all prompts): mean {np.mean(lifts):+.4f} "
          f"({sum(1 for l in lifts if l > 0)}/{len(lifts)} prompts positive)")
    gate = np.mean(rule_hits) > np.mean(base_rates) and np.mean(lifts) > 0
    print(f"GATE: {'PASSED' if gate else 'FAILED'} "
          "(retrieval must beat random on rules AND show positive semantic lift)")


if __name__ == "__main__":
    main()
