# Campaign v1: friction gesture, velocity genes, axis calibration (2026-08-01, evening)

Engine at the friction-exciter commit; atlas `runs/c1.sqlite` (M4 CPU, 175 s: 1024
genomes × 4 probes, 3225/4096 renders gated in).

## Engine: second gesture shipped

`friction` exciter type: sustained colored-noise excitation with stick-slip grain train
(roughness, grit_rate_hz), unit-RMS normalized, drive compensated by 1/t60 (steady-state
amplitude of a driven resonator grows ∝ τ — uncompensated, long bodies clipped at peak
5.0). Contract tests: sustain-while-held (within 12 dB across the held note), decay after
note-off, byte-exact determinism. First sustained sounds rendered: bowed-glass pad
chords, scraped-metal drone.

## Genome v1

Velocity mappings (`to_level/to_brightness/to_hardness`) and exciter type + friction
params are now genes (18 searchable). Motivated by behavior-contract data: v0 fixed
velocity constants left vel→centroid inverted for 37% of the space.

## Results vs c0

| metric | c0 | c1 |
|---|---|---|
| held-out rule hit-rate@3 (vs random) | 0.694 vs 0.287 | 0.694 vs 0.288 |
| semantic lift (mean, 30 prompts positive) | +0.164, 30/30 | **+0.197, 30/30** |
| vel→RMS rho median | +0.98 | +0.93 (6% negative — friction ladders, investigate) |
| vel→centroid rho ≥ 0.7 | 16% | **29%** (median +0.14 → +0.24) |
| friction skins | 0 | 310 |
| "harsh noisy metallic scrape" top-3 | impossible (no gesture) | all friction |
| "a bowed string drone" top-3 | n/a | all friction |

Anchors indexed on all four probes (percussion anchors legitimately fail the sustain
probe's silence gate — best-probe-match scoring makes that harmless).

## Axis calibration against NSynth human quality labels (`exp0/axis_calibration.json`)

| axis | AUC vs human labels | verdict |
|---|---|---|
| brightness (contrastive bright−dark) | **1.000** | strong — CLAP judges |
| percussiveness (contrastive) | **0.995** | strong — CLAP judges |
| decay speed (contrastive) | 0.493 | unreliable — **features judge decay, never CLAP** |

First entry of the versioned per-axis "who judges what" evaluator config.

## Known issues / next

1. **Friction-dominance bias:** friction skins top even percussive prompts
   ("warm glass percussion…" top-3 all friction, cos ~0.52). Fix: wire the calibrated
   percussiveness axis into the constraint rerank for strike-words.
2. Batch eval (`retrieval.py`) still scores via mean-pooled skin vectors without
   constraints; the lab uses v0.2 best-probe + constraints. Unify on `query.py`.
3. 6% negative vel→RMS rho appeared with friction ladders — characterize.
4. Human gate re-votes on the three fixed prompts still pending (lab now serves c1).
