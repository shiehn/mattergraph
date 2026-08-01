# Campaign v2: the four-gesture space (2026-08-01, late)

Atlas `runs/c2.sqlite`: 1024 Sobol genomes over the expanded genome (23 genes,
4 exciter types: noise_burst / friction / periodic / sample) × 4 probes, plus
all 8 anchors indexed. Sample exciters draw from the owned-pack bank
(assets/exciters, 60 prompt-labeled transients).

## Held-out gate: PASSED (0.667 @3 vs 0.283 random, 2.35×; semantic lift +0.203, 30/30 positive)

Headline: **"harsh noisy metallic scrape" — 0.00 across c0, now 1.00.** The
capacity gap the evaluation surfaced three campaigns ago is closed and verified.

Note honestly: rule hit-rate dipped slightly vs c1 (0.694 → 0.667) — the space
is now far more diverse (tonal/sample skins dilute percussion density) while the
rule prompts remain percussion-slanted. Semantic lift stayed strong. Expected
diversification cost; the QD archive (not raw Sobol) is the retrieval substrate
going forward anyway.

## Velocity-expressiveness trend (behavior contracts)

| campaign | vel→centroid median rho | ≥ 0.7 |
|---|---|---|
| c0 (fixed velocity constants) | +0.14 | 16% |
| c1 (velocity genes) | +0.24 | 29% |
| **c2 (velocity genes + 4 gestures)** | **+0.45** | **40%** |

The searched mappings keep compounding: two-and-a-half× more skins now satisfy
a strong "brighter when harder" contract than at v0.

## Next

1. QD re-run over the four-gesture space — measure how many of the 74 empty
   niches the new gestures claim (the archive is the real deliverable).
2. Percentile nameability + contrast prompts in QD fitness (ceiling fix).
3. Steve: in-app A/B of the integration branch; lab curation pass; listen to
   periodic_fat_bass / sampled_snare_glass anchors.
