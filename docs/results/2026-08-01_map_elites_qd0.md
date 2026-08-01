# MAP-Elites qd0: quality-diversity over the genome space (2026-08-01, night)

`mattergraph_research.qd`: archive over interpretable descriptor bins (plan §13.2 —
decay 6 log bins × centroid 6 log bins × flatness 4 bins = 144 niches). Fitness =
0.5 × playability (velocity-contract rhos) + 0.5 × nameability (best CLAP cosine
against a 22-prompt vocabulary bank, /0.6 clipped). Seeded from campaign c1; elite
mutations (rate 0.35, σ 15%) + Sobol immigrants (~1/6 of each batch); every candidate
renders 3 probes + the velocity ladder through the real binary. Archive checkpoints
每 batch; output atlas is self-contained (c1 copied in).

## Results (2016 evals, 492 s, M4 CPU)

| metric | seed (c1 Sobol) | after MAP-Elites |
|---|---|---|
| niche coverage | 44/144 | **70/144** (+26 cells Sobol never reached) |
| QD-score | 38.2 | **66.3** (+74%) |
| elite improvements | — | 193 |
| held-out retrieval hit-rate@3 | 0.694 (2.41×) | **0.750 (2.59×)** |
| semantic lift (30/30 positive both) | +0.197 | **+0.229** |

Search demonstrably beats sampling: same engine, same evaluator, better atlas.

## Two findings that set the next work

1. **Coverage plateau = capacity map.** The final ~150 evals added zero new cells:
   the 74 still-empty niches (dominant patterns: sustained-noisy, very-long-decay ×
   high-flatness, extreme-bright × long) are regions the current gesture set cannot
   reach. This is the plan's §14 capacity-failure evidence arriving on schedule — and
   the queued exciter expansion (periodic + sample-excited, task #18) targets exactly
   those regions.
2. **Fitness ceiling.** Top evolved elites saturate at fitness 1.000 (playability
   AND nameability clipped). The function has stopped discriminating at the top, and
   saturated CLAP-nameability is where evaluator-gaming risk begins (plan §12.5).
   Next QD iteration: normalize nameability by corpus percentile instead of the fixed
   0.6 ceiling, add negative/contrast prompts to the vocabulary bank, and consider a
   human-preference term once gate votes accumulate.

## Artifacts

- `runs/qd0.sqlite` (atlas: 1149 skins incl. anchors + evolved elites; lab serves it)
- `runs/qd0.archive.json` (archive checkpoint: cell → elite + fitness)
- Two evolved-elite renders delivered for informal audition.
