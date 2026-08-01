# Research loop v0 — first campaign and Phase-2 gate (2026-08-01)

Machine: Apple M4 laptop, CPU only. Engine at commit 811c1fb.

## Campaign

- 1024 Sobol genomes over the v0 macro space (`mattergraph_research.genome`), two
  diagnostic probes each (`diag_strike`, `diag_arp3`).
- **90 seconds wall time** for render + hard gates + features + CLAP embedding
  (~11 genomes/s). 1996/2048 renders passed gates (rejects: uncontrolled energy /
  silence — the gates are live).
- Evaluator health gate ran first and passed (mandatory preflight; see exp0/RESULTS.md).
- Atlas: `runs/c0.sqlite` (gitignored; fully reproducible from seed 0 by determinism).

## Phase-2 retrieval gate (plan Rev4 §Phase 2): **PASSED**

30 held-out prompts (`mattergraph_research.heldout`), retrieval = CLAP text embedding →
cosine over skin embeddings. Feature rules never touch the retrieval path, so the
hit-rate comparison is uncontaminated.

- **Rule prompts: top-3 hit-rate 0.694 vs 0.287 random baseline — 2.42× lift.**
- **Semantic lift positive on 30/30 prompts** (mean +0.164 cosine over corpus mean).
- Standouts: glass 1.00 vs 0.15 base; bells ~0.56 vs 0.06 base (~9× lift); drums ~0.89 vs 0.29.
- Honest misses: "warm wooden knock" 0.00 (the known CLAP wood/drum adjacency, exp0);
  "harsh noisy metallic scrape" 0.00 (engine has no scrape/friction gesture yet — a
  capacity gap for the node roadmap, correctly surfaced by a held-out prompt);
  "short staccato click" 0.00 vs 0.47 base (retrieval favors tonal matches over
  duration—axis weighting to revisit).

Remaining half of the Phase-2 exit: human audition of top-1 results ("≥50% plausibly
what I asked for") — pending owner listening session. Four top-1 renders were produced
for the audition (glass chime / drum thump / metal bell / shimmering strike prompts).

## Sustain track step 0 (Experiment 0-S): **GATE NOT PASSED**

See `research/exp0/RESULTS_SUSTAINED.md`. Sustained archetype identification 1/4
(everything reads as "organ"; stimulus-vs-evaluator confound unresolved), raw "bright"
pole inverted on pads while the **contrastive** bright−dark score is perfectly monotonic,
evolution axis unusable. Consequences: no sustain-track pod rental yet; project-wide rule
adopted — semantic axes are always scored with contrastive prompt pairs of verified
polarity. Next local step: real-sample discrimination test (NSynth labels or Surge
renders) to separate the confound.

## Throughput planning numbers (measured, M4)

- Render+gate+features: ~45 ms/genome-probe.
- CLAP embed: ~35 ms/clip batched (better than the 75 ms single-clip estimate).
- Extrapolation: 100k-genome campaign ≈ 2.5 h on this laptop; the RunPod decision rule
  ("wouldn't finish overnight") triggers around the million-genome scale or when probes
  get longer/more numerous.
